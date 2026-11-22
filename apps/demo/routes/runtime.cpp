#include "runtime.hpp"

#include <chrono>
#include <string>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "../services.hpp"

namespace demo {

asio::awaitable<void> runtime_controller::io(fut::response& out) {
    bool fired = false;
    if (g_io != nullptr) {
        asio::steady_timer timer{*g_io};
        timer.expires_after(std::chrono::milliseconds{1});
        co_await timer.async_wait(asio::use_awaitable);
        fired = true;
    }
    out.dto(io_out{g_io != nullptr, fired});
    co_return;
}

asio::awaitable<void> runtime_controller::write_log(fut::response& out) {
    log.info("demo /log was called");
    log.warning("and this one is a warning");
    audit_log.info("audited by the explicitly named group");
    out.dto(log_out{true, std::string(log.name())});
    co_return;
}

asio::awaitable<void> runtime_controller::audit_group(fut::response& out) {
    out.dto(log_out{audit_log.sink() != nullptr, std::string(audit_log.name())});
    co_return;
}

asio::awaitable<void> runtime_controller::error(std::string kind, fut::response& out) {
    if (kind == "400") throw fut::bad_request("the client sent something unusable");
    if (kind == "401") throw fut::unauthorized("no token on this route");
    if (kind == "403") throw fut::forbidden("this is not yours");
    if (kind == "404") throw fut::not_found("nothing lives at that path");
    if (kind == "409") throw fut::conflict("that already exists");
    if (kind == "418") throw fut::http_error(418, "i am a teapot");
    if (kind == "500") throw std::runtime_error("internal detail that must not leak");
    throw fut::bad_request("unknown error kind '" + kind + "'");
}

} // namespace demo
