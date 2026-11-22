#pragma once

// The main controller: no class-level path, so its routes sit directly under
// the app prefix /api/demo. It covers the return shapes and the simple
// bindings (query, header, a plain request parameter).

#include "../common.hpp"

#include "../dto.hpp"

namespace demo {

struct [[=fut::controller{}]] demo_controller {

    // a DTO becomes JSON
    [[=fut::http_get("/ping")]]
    asio::awaitable<pong> ping() { co_return pong{true, "demo"}; }

    // reflection over the app: the route list comes from the annotations
    [[=fut::http_get("/info")]]
    asio::awaitable<info_out> info() {
        co_return info_out{FUT_APP_NAME, FUT_APP_PREFIX, fut::abi_version, all_routes()};
    }

    // a std::string is served as text/plain
    [[=fut::http_get("/text")]]
    asio::awaitable<std::string> text() {
        co_return "fut demo app -- no route table, no recompiled server";
    }

    // a fut::response keeps full control of status, headers and body
    [[=fut::http_get("/custom")]]
    asio::awaitable<fut::response> custom() {
        fut::response res;
        res.status = 201;
        res.headers.emplace_back("X-Demo-Custom", "1");
        res.headers.emplace_back("Location", "/api/demo/ping");
        res.body = R"({"created":true})";
        co_return res;
    }

    // from_query, one parameter at a time; a missing one is a 400
    [[=fut::http_get("/query")]]
    asio::awaitable<greeting_out> query([[=fut::from_query("name")]] std::string name,
                                        [[=fut::from_query("count")]] int count) {
        co_return greeting_out{name + "!", count * 2, count * (count + 1) / 2};
    }

    // from_query on a whole DTO: absent members keep their defaults
    [[=fut::http_get("/query/dto")]]
    asio::awaitable<query_in> query_dto([[=fut::from_query{}]] query_in in) {
        co_return in;
    }

    // from_header
    [[=fut::http_get("/header")]]
    asio::awaitable<pong> header([[=fut::from_header("X-Token")]] std::string token) {
        co_return pong{!token.empty(), token};
    }

    // a plain request parameter needs no binding annotation
    [[=fut::http_get("/request")]]
    asio::awaitable<request_out> request_info(const fut::request& req) {
        co_return request_out{std::string(req.method), std::string(req.path),
                              std::string(req.query), req.headers.size()};
    }
};

} // namespace demo
