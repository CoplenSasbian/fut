#include "server.hpp"

#include <utility>
#include <vector>

#include <fut/json.hpp>
#include <fut/log.hpp>

namespace fut::exec {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

namespace {

constexpr auto srv_log = fut::log::grp("server");

// co_spawn completion handler: log exceptions, keep other sessions alive.
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

asio::awaitable<void> run_session(beast::tcp_stream stream, router rt,
                                  std::chrono::seconds timeout) {
    beast::flat_buffer buf;
    for (;;) {
        http::request<http::string_body> req;
        stream.expires_after(timeout);
        try {
            co_await http::async_read(stream, buf, req, asio::use_awaitable);
        } catch (const boost::system::system_error&) {
            break;  // peer closed the connection / read timeout
        }

        const bool keep_alive = req.keep_alive();

        // Header views for fut::request (valid while req is alive).
        std::vector<std::pair<std::string_view, std::string_view>> hdrs;
        hdrs.reserve(16);
        for (const auto& h : req.base()) hdrs.emplace_back(h.name_string(), h.value());

        fut::response res;
        const std::string_view method = req.method_string();
        const router::match m = rt.route(req.target());
        if (!m.app) {
            res = fut::error(404, "no route");
        } else {
            bool found = false;
            for (const fut::route_record& r : m.app->entries()) {
                if (r.method != method) continue;
                fut::path_match pm;
                if (!fut::match_pattern(r.pattern, m.rel_path, pm)) continue;
                found = true;
                try {
                    res = co_await r.invoke(fut::request{
                        method, req.target(), m.rel_path, m.query,
                        req.body(), hdrs}, pm);
                } catch (const fut::bind_error& e) {
                    res = fut::error(400, e.what());
                }
                break;
            }
            if (!found) res = fut::error(404, "no route");
        }

        http::response<http::string_body> out;
        out.result(res.status);
        out.version(req.version());
        out.keep_alive(keep_alive);
        out.set(http::field::server, "fut");
        out.set(http::field::content_type, res.content_type);
        for (const auto& h : res.headers) out.set(h.first, h.second);
        out.body() = std::move(res.body);
        out.prepare_payload();

        co_await http::async_write(stream, out, asio::use_awaitable);
        if (!keep_alive) break;
    }

    boost::system::error_code ec;
    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
}

} // namespace

server::server(asio::io_context& ioc, std::string host, unsigned short port,
               router rt, std::chrono::seconds timeout)
    : ioc_(ioc),
      host_(std::move(host)),
      port_(port),
      acceptor_(ioc),
      router_(std::move(rt)),
      timeout_(timeout) {}

asio::awaitable<void> server::listen() {
    using asio::ip::tcp;
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
        asio::co_spawn(ioc_,
                       run_session(beast::tcp_stream(std::move(sock)), router_, timeout_),
                       log_exception);
    }
}

} // namespace fut::exec
