#pragma once

// Routes built on injected services, and the error mapping.

#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller{}]] runtime_controller {

    [[=fut::http_get("/io")]]
    asio::awaitable<void> io(fut::response& out);

    [[=fut::http_get("/log")]]
    asio::awaitable<void> write_log(fut::response& out);

    [[=fut::http_get("/audit-group")]]
    asio::awaitable<void> audit_group(fut::response& out);

    // The whole error mapping in one route: the kind names the failure.
    [[=fut::http_get("/error/{kind}")]]
    asio::awaitable<void> error([[=fut::from_path("kind")]] std::string kind,
                                fut::response& out);
};

} // namespace demo
