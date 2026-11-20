#include "server.hpp"

#include <cstdio>
#include <utility>
#include <vector>

#include <fut/json.hpp>

namespace fut::exec {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

namespace {

// co_spawn 完成回调：异常只打印，不打断其它会话
void log_exception(std::exception_ptr ep) {
    if (!ep) return;
    try {
        std::rethrow_exception(std::move(ep));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[session] exception: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "[session] unknown exception\n");
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
            break; // 对端关闭 / 读超时：安静结束会话
        }

        const bool keep_alive = req.keep_alive();

        // 供 fut::request 使用的头视图（req 存活期间有效）
        std::vector<std::pair<std::string_view, std::string_view>> hdrs;
        hdrs.reserve(16);
        for (const auto& h : req.base()) hdrs.emplace_back(h.name_string(), h.value());

        fut::response res;
        try {
            const router::match m = rt.route(req.target());
            if (!m.app) {
                res = fut::error(404, "no route");
            } else {
                fut::request f_req;
                f_req.method  = req.method_string();
                f_req.target  = req.target();
                f_req.path    = m.rel_path;
                f_req.query   = m.query;
                f_req.body    = req.body();
                f_req.headers = hdrs;
                res = co_await m.app->handle(f_req);
            }
        } catch (const std::exception& e) {
            res = fut::error(500, e.what());
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
    std::fprintf(stdout, "[server] listening on http://%s:%u\n", host_.c_str(), (unsigned)port_);

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
