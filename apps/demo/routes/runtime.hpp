#pragma once

// Routes built on injected services, and the error mapping.
//
// Everything here throws to fail: the router is the single place that turns an
// exception into a status and a body.

#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller{}]] runtime_controller {

    // The injected io_context is the server's live loop: a timer created on it
    // can only fire if that loop is running.
    [[=fut::http_get("/io")]]
    asio::awaitable<io_out> io();

    // The injected log groups write into the server's chains: one named after
    // the variable ("demo.log"), one named by the annotation ("demo.audit").
    [[=fut::http_get("/log")]]
    asio::awaitable<log_out> write_log();

    // The explicit name is visible on the object itself.
    [[=fut::http_get("/audit-group")]]
    asio::awaitable<log_out> audit_group();

    // a chosen status: fut::http_error keeps it
    [[=fut::http_get("/fail/{code}")]]
    asio::awaitable<pong> fail([[=fut::from_path("code")]] int code);

    // anything unexpected becomes a 500; the detail stays in the log
    [[=fut::http_get("/boom")]]
    asio::awaitable<pong> boom();
};

} // namespace demo
