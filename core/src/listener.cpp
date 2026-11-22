#include "listener.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include <fut/json.hpp>
#include <fut/log.hpp>

#include "http_parser.hpp"

namespace fut::exec {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace {

constexpr auto srv_log = fut::log::grp("server");

constexpr std::size_t read_chunk = 16 * 1024;
constexpr std::size_t max_body = 32 * 1024 * 1024;

void log_exception(std::exception_ptr ep) {
    if (!ep) return;
    try {
        std::rethrow_exception(std::move(ep));
    } catch (const std::exception& e) {
        srv_log.error("session exception: {}", e.what());
    } catch (...) {
        srv_log.error("session exception: unknown");
    }
}

// Reads up to `dst.size()` bytes with a deadline. Returns the byte count, or
// 0 when the peer closed or the timeout fired.
asio::awaitable<std::size_t> read_some(tcp::socket& socket, asio::mutable_buffer dst,
                                       std::chrono::seconds timeout) {
    asio::steady_timer deadline(co_await asio::this_coro::executor);
    deadline.expires_after(timeout);
    deadline.async_wait([&socket](const boost::system::error_code& ec) {
        if (!ec) {
            boost::system::error_code ignored;
            socket.close(ignored);   // unblocks the pending read
        }
    });

    auto [ec, n] = co_await socket.async_read_some(dst, asio::as_tuple(asio::use_awaitable));
    deadline.cancel();
    co_return ec ? 0 : n;
}

asio::awaitable<void> write_all(tcp::socket& socket, std::string_view data) {
    co_await asio::async_write(socket, asio::buffer(data), asio::use_awaitable);
}

// Parsed message -> fut::request. The router owns matching, the middleware
// chains and the error mapping (400/404/405/500).
// The connection's side of fut::response: one bound response per session,
// reused for every request on it. The head and the body are written here and
// nowhere else.
class response_writer {
public:
    response_writer(tcp::socket& socket, std::string& out) : socket_(socket), out_(out) {}

    fut::response::sink sink() {
        fut::response::sink s;
        s.begin = [this](const fut::response& head) -> asio::awaitable<void> {
            serialize_stream_head(out_, head.status, head.content_type, head.headers);
            co_await write_all(socket_, out_);
        };
        s.chunk = [this](std::string_view bytes) -> asio::awaitable<void> {
            out_.clear();
            append_chunk(out_, bytes);
            co_await write_all(socket_, out_);
        };
        s.finish = [this]() -> asio::awaitable<void> {
            out_.clear();
            append_last_chunk(out_);
            co_await write_all(socket_, out_);
        };
        s.whole = [this](const fut::response& r) -> asio::awaitable<void> {
            serialize_response(out_, r.status, r.content_type, r.headers, r.body, true,
                               r.is_head_only());
            co_await write_all(socket_, out_);
        };
        return s;
    }

private:
    tcp::socket& socket_;
    std::string& out_;
};

asio::awaitable<void> dispatch(router* rt, const http_request_message& msg,
                               std::string_view remote, fut::response* conn_response) {
    const std::string_view target = msg.target;
    const auto qpos = target.find('?');
    const std::string_view path =
        (qpos == std::string_view::npos) ? target : target.substr(0, qpos);
    const std::string_view query =
        (qpos == std::string_view::npos) ? std::string_view{} : target.substr(qpos + 1);

    // The message already stores its headers as views into its own buffer, so
    // the request spans that list directly -- no per-request copy at all.
    fut::request req{msg.method, target, path, query, msg.body, msg.header_span(), remote};
    co_await rt->dispatch(req, *conn_response);
}

asio::awaitable<void> run_session(tcp::socket socket, router* rt,
                                  std::chrono::seconds timeout) {
    // "ip:port" of the peer, kept for the whole session so the request can
    // carry a stable view.
    std::string remote;
    try {
        const auto endpoint = socket.remote_endpoint();
        remote = endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
    } catch (const std::system_error&) {
        remote = "<unknown>";
    }

    request_parser parser;

    std::string buffer;          // bytes received but not yet parsed
    buffer.reserve(read_chunk * 2);
    std::array<char, read_chunk> chunk{};

    // Reused across the requests of this connection: the serialized bytes and
    // the response object that writes them.
    std::string out;
    out.reserve(1024);
    response_writer writer{socket, out};
    const fut::response::sink sink = writer.sink();
    fut::response conn_response;
    conn_response.bind(&sink);

    for (;;) {
        // 1) Parse everything already buffered (pipelined requests too).
        bool keep_going = true;
        while (keep_going && !buffer.empty()) {
            switch (parser.feed(buffer.data(), buffer.size())) {
            case request_parser::status::need_more:
                buffer.clear();   // all bytes consumed; wait for more
                keep_going = false;
                break;

            case request_parser::status::interim: {
                buffer.erase(0, parser.consumed());
                const http_request_message& msg = parser.message();

                if (msg.headers_too_large || msg.target_too_large) {
                    const unsigned status = msg.headers_too_large ? 431 : 414;
                    const std::string_view body = msg.headers_too_large
                        ? R"({"error":"request header fields too large"})"
                        : R"({"error":"request target too long"})";
                    serialize_response(out, status, "application/json", {}, body, false);
                    co_await write_all(socket, out);
                    co_return;
                }

                // The client is waiting before it sends the body: say go.
                static constexpr std::string_view go = "HTTP/1.1 100 Continue\r\n\r\n";
                co_await write_all(socket, go);
                parser.resume();
                break;   // keep parsing this buffer
            }

            case request_parser::status::error:
                if (const auto err = parser.error(); !err.empty())
                    srv_log.warning("bad request from peer: {}", std::string(err));
                serialize_response(out, 400, "application/json", {},
                                   R"({"error":"bad request"})", false);
                co_await write_all(socket, out);
                co_return;

            case request_parser::status::complete: {
                buffer.erase(0, parser.consumed());
                const http_request_message& msg = parser.message();
                if (msg.body.size() > max_body) {
                    serialize_response(out, 413, "application/json", {},
                                       R"({"error":"body too large"})", false);
                    co_await write_all(socket, out);
                    co_return;
                }

                const bool keep_alive = msg.keep_alive;
                conn_response.reset();
                co_await dispatch(rt, msg, remote, &conn_response);
                co_await conn_response.end();
                if (!keep_alive) co_return;
                parser.reset();
                break;
            }
            }
        }

        // 2) Read more.
        const std::size_t n = co_await read_some(socket, asio::buffer(chunk), timeout);
        if (n == 0) co_return;
        buffer.append(chunk.data(), n);
    }
}

} // namespace

listener::listener(asio::io_context& ioc, std::string host, unsigned short port,
                   router& rt, std::chrono::seconds timeout)
    : ioc_(ioc),
      host_(std::move(host)),
      port_(port),
      acceptor_(ioc),
      router_(rt),
      timeout_(timeout) {}

asio::awaitable<void> listener::listen() {
    const tcp::endpoint ep{asio::ip::make_address(host_), port_};
    acceptor_.open(ep.protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen(asio::socket_base::max_listen_connections);
    srv_log.info("listening on http://{}:{}", host_, (unsigned)port_);

    for (;;) {
        auto [ec, sock] = co_await acceptor_.async_accept(asio::as_tuple(asio::use_awaitable));
        if (ec) {
            if (ec == asio::error::operation_aborted) co_return;
            continue;
        }
        asio::co_spawn(ioc_, run_session(std::move(sock), &router_, timeout_), log_exception);
    }
}

} // namespace fut::exec
