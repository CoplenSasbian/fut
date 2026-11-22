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

asio::awaitable<pong> runtime_controller::fail(int code) {
    throw fut::http_error(static_cast<unsigned>(code),
                          "requested failure " + std::to_string(code));
}

asio::awaitable<pong> runtime_controller::boom() {
    throw std::runtime_error("something the client must not see");
}

} // namespace demo
