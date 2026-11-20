#pragma once

// /api/web -- sample front-end-facing app, controller style.
//
// This header carries the annotated controller DECLARATIONS; definitions live
// in web_routes.cpp as an ordinary translation unit. The CMake-generated
// export unit scans namespace web (recursively -- the beta controller sits
// in a nested namespace) and registers the routes; the add_app() prefix
// arrives as FUT_APP_PREFIX and is composed into every path.

#include <string>

#include <fut/http/http.hpp>


namespace asio = boost::asio;

struct pong_dto { bool pong{true}; std::string app{"web"}; };
struct hello_out { std::string hello{}; };
struct time_out { long long epoch_seconds{}; };

struct calc_in {
    double a{};
    double b{};
    std::string op{};
};

struct calc_out {
    double result{};
    std::string op{};
};

struct sum_out {
    long long a{};
    long long b{};
    long long sum{};
};

namespace web {

// General routes: an empty controller path keeps the method patterns
// unchanged (/ping, /hello, /time, /calc, /sum/{a}/{b}, /note).
struct [[=fut::controller{}]] web_controller {

    [[=fut::http_get("/ping")]]
    asio::awaitable<pong_dto> ping();

    // empty key -> falls back to the parameter name ("name")
    [[=fut::http_get("/hello")]]
    asio::awaitable<hello_out> hello([[=fut::from_query{}]] std::string name);

    [[=fut::http_get("/time")]]
    asio::awaitable<time_out> time();

    [[=fut::http_post("/calc")]]
    asio::awaitable<calc_out> calc([[=fut::from_body{}]] calc_in in);

    [[=fut::http_get("/sum/{a}/{b}")]]
    asio::awaitable<sum_out> sum([[=fut::from_path("a")]] long long a,
                                 [[=fut::from_path("b")]] long long b);

    // plain std::string return -> served as text/plain
    [[=fut::http_get("/note")]]
    asio::awaitable<std::string> note();
};

namespace beta {

// A controller inside a nested namespace: the recursive scan finds it, and
// its class path composes with the method path -> /beta/note.
struct [[=fut::controller("/beta")]] beta_controller {
    [[=fut::http_get("/note")]]
    asio::awaitable<std::string> note();
};

} // namespace beta

} // namespace web
