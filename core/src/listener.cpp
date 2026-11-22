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
asio::awaitable<fut::response> dispatch(router* rt, const http_request_message& msg) {
    const std::string_view target = msg.target;
    const auto qpos = target.find('?');
    const std::string_view path =
        (qpos == std::string_view::npos) ? target : target.substr(0, qpos);
    const std::string_view query =
        (qpos == std::string_view::npos) ? std::string_view{} : target.substr(qpos + 1);

    std::vector<std::pair<std::string_view, std::string_view>> headers;
    headers.reserve(msg.headers.size());
    for (const auto& [name, value] : msg.headers) headers.emplace_back(name, value);

    co_return co_await rt->dispatch(fut::request{msg.method, target, path, query, msg.body,
                                                 headers});
}

asio::awaitable<void> run_session(tcp::socket socket, router* rt,
                                  std::chrono::seconds timeout) {
    request_parser parser;

    std::string buffer;          // bytes received but not yet parsed
    buffer.reserve(read_chunk * 2);
    std::array<char, read_chunk> chunk{};

    for (;;) {
        // 1) Parse everything already buffered (pipelined requests too).
        bool keep_going = true;
        while (keep_going && !buffer.empty()) {
            switch (parser.feed(buffer.data(), buffer.size())) {
            case request_parser::status::need_more:
                buffer.clear();   // all bytes consumed; wait for more
                keep_going = false;
                break;

            case request_parser::status::error:
                if (const auto err = parser.error(); !err.empty())
                    srv_log.warning("bad request from peer: {}", std::string(err));
                co_await write_all(socket,
                                   serialize_response(400, "application/json",
                                                      {}, R"({"error":"bad request"})", false));
                co_return;

            case request_parser::status::complete: {
                buffer.erase(0, parser.consumed());
                const http_request_message& msg = parser.message();
                if (msg.body.size() > max_body) {
                    co_await write_all(socket,
                                       serialize_response(413, "application/json", {},
                                                          R"({"error":"body too large"})", false));
                    co_return;
                }

                const bool keep_alive = msg.keep_alive;
                const fut::response res = co_await dispatch(rt, msg);
                co_await write_all(socket,
                                   serialize_response(res.status, res.content_type, res.headers,
                                                      res.body, keep_alive));
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
