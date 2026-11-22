#pragma once

// The main controller: no class-level path, so its routes sit directly under
// the app prefix /api/demo. Every action writes the connection's response.

#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller{}]] demo_controller {

    [[=fut::http_get("/ping")]]
    asio::awaitable<void> ping(fut::response& out);

    [[=fut::http_get("/info")]]
    asio::awaitable<void> info(fut::response& out);

    [[=fut::http_get("/text")]]
    asio::awaitable<void> text(fut::response& out);

    [[=fut::http_get("/custom")]]
    asio::awaitable<void> custom(fut::response& out);

    [[=fut::http_get("/query")]]
    asio::awaitable<void> query([[=fut::from_query("name")]] std::string name,
                                [[=fut::from_query("count")]] int count,
                                fut::response& out);

    [[=fut::http_get("/query/dto")]]
    asio::awaitable<void> query_dto([[=fut::from_query{}]] query_in in,
                                    fut::response& out);

    [[=fut::http_get("/header")]]
    asio::awaitable<void> header([[=fut::from_header("X-Token")]] std::string token,
                                 fut::response& out);

    [[=fut::http_get("/request")]]
    asio::awaitable<void> request_info(const fut::request& req, fut::response& out);

    [[=fut::http_get("/secure/ping")]]
    asio::awaitable<void> secure_ping(fut::response& out);
};

} // namespace demo
