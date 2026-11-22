#pragma once

// Routes built on injected services, and the error mapping.
//
// Everything here throws to fail: the router is the single place that turns an
// exception into a status and a body.

#include <chrono>

#include "../common.hpp"
#include <fut/log.hpp>

#include "../dto.hpp"
#include "../services.hpp"

namespace demo {

struct [[=fut::controller{}]] service_controller {

    // The injected io_context is the server's live loop: a timer created on it
    // can only fire if that loop is running.
    [[=fut::http_get("/io")]]
    asio::awaitable<io_out> io() {
        if (g_io == nullptr) co_return io_out{false, false};
        asio::steady_timer timer(*g_io);
        timer.expires_after(std::chrono::milliseconds(1));
        co_await timer.async_wait(asio::use_awaitable);
        co_return io_out{true, true};
    }

    // The injected log groups write into the server's chains: one named after
    // the variable ("demo.log"), one named by the annotation ("demo.audit").
    [[=fut::http_get("/log")]]
    asio::awaitable<log_out> write_log() {
        log.info("demo /log was called");
        log.warning("and this one is a warning");
        audit_log.info("audited by the explicitly named group");
        co_return log_out{true, std::string(log.name())};
    }

    // The explicit name is visible on the object itself.
    [[=fut::http_get("/audit-group")]]
    asio::awaitable<log_out> audit_group() {
        co_return log_out{audit_log.sink() != nullptr, std::string(audit_log.name())};
    }

    // a chosen status: fut::http_error keeps it
    [[=fut::http_get("/fail/{code}")]]
    asio::awaitable<pong> fail([[=fut::from_path("code")]] int code) {
        throw fut::http_error(static_cast<unsigned>(code),
                              "requested failure " + std::to_string(code));
    }

    // anything unexpected becomes a 500; the detail stays in the log
    [[=fut::http_get("/boom")]]
    asio::awaitable<pong> boom() {
        throw std::runtime_error("something the client must not see");
    }
};

} // namespace demo
