#include "runtime.hpp"

#include <chrono>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "../services.hpp"

namespace demo {

asio::awaitable<io_out> runtime_controller::io() {
    if (g_io == nullptr) co_return io_out{false, false};
    asio::steady_timer timer(*g_io);
    timer.expires_after(std::chrono::milliseconds(1));
    co_await timer.async_wait(asio::use_awaitable);
    co_return io_out{true, true};
}

asio::awaitable<log_out> runtime_controller::write_log() {
    log.info("demo /log was called");
    log.warning("and this one is a warning");
    audit_log.info("audited by the explicitly named group");
    co_return log_out{true, std::string(log.name())};
}

asio::awaitable<log_out> runtime_controller::audit_group() {
    co_return log_out{audit_log.sink() != nullptr, std::string(audit_log.name())};
}

asio::awaitable<pong> runtime_controller::error(std::string kind) {
    // A thrown error carries its own status; the helpers just name it.
    if (kind == "400") throw fut::bad_request("the client sent something unusable");
    if (kind == "401") throw fut::unauthorized("no token on this route");
    if (kind == "403") throw fut::forbidden("this is not yours");
    if (kind == "404") throw fut::not_found("nothing lives at that path");
    if (kind == "409") throw fut::conflict("that already exists");
    if (kind == "418") throw fut::http_error(418, "i am a teapot");
    // Anything else is a 500: the client sees the generic text, the log sees
    // the detail.
    if (kind == "500") throw std::runtime_error("internal detail that must not leak");
    throw fut::bad_request("unknown error kind '" + kind + "'");
}

} // namespace demo
