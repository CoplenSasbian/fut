#pragma once

// The main controller: no class-level path, so its routes sit directly under
// the app prefix /api/demo. It covers the return shapes and the simple
// bindings (query, header, a plain request parameter).
//
// Declarations only -- the annotations live here because the scan reads
// declarations; the bodies are in core.cpp.

#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller{}]] demo_controller {

    // a DTO becomes JSON
    [[=fut::http_get("/ping")]]
    asio::awaitable<pong> ping();

    // reflection over the app: the route list comes from the annotations
    [[=fut::http_get("/info")]]
    asio::awaitable<info_out> info();

    // a std::string is served as text/plain
    [[=fut::http_get("/text")]]
    asio::awaitable<std::string> text();

    // a fut::response keeps full control of status, headers and body
    [[=fut::http_get("/custom")]]
    asio::awaitable<fut::response> custom();

    // from_query, one parameter at a time; a missing one is a 400
    [[=fut::http_get("/query")]]
    asio::awaitable<greeting_out> query([[=fut::from_query("name")]] std::string name,
                                        [[=fut::from_query("count")]] int count);

    // from_query on a whole DTO: absent members keep their defaults
    [[=fut::http_get("/query/dto")]]
    asio::awaitable<query_in> query_dto([[=fut::from_query{}]] query_in in);

    // from_header
    [[=fut::http_get("/header")]]
    asio::awaitable<pong> header([[=fut::from_header("X-Token")]] std::string token);

    // a plain request parameter needs no binding annotation
    [[=fut::http_get("/request")]]
    asio::awaitable<request_out> request_info(const fut::request& req);

    // Protected by the app middleware: without X-Demo-Token this is a 401 and
    // the handler never runs.
    [[=fut::http_get("/secure/ping")]]
    asio::awaitable<pong> secure_ping();
};

} // namespace demo
