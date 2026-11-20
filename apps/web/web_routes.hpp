#pragma once

// /api/web -- sample front-end-facing app (declaration/definition split).
//
// This header carries the annotated DECLARATIONS inside namespace web (the
// namespace named after the app). The CMake-generated export unit includes
// it and scans -- recursively, so the nested 'beta' group works too.
// Definitions live in web_routes.cpp, compiled as an ordinary translation
// unit; the generated unit's calls resolve through the linker. Including
// this header anywhere else never scans anything.

#include <string>

#include <fut/annotate.hpp>
#include <fut/refl_router.hpp>

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

[[=fut::http_get("/ping")]]
asio::awaitable<pong_dto> ping();

// empty key -> falls back to the parameter name ("name")
[[=fut::http_get("/hello")]]
asio::awaitable<hello_out> hello([[=fut::from_query{}]] std::string name);

[[=fut::http_get("/time")]]
asio::awaitable<time_out> time();

[[=fut::http_post("/calc")]]
asio::awaitable<calc_out> calc([[=fut::from_body{}]] calc_in in);

// path placeholder binding: "/sum/{a}/{b}"
[[=fut::http_get("/sum/{a}/{b}")]]
asio::awaitable<sum_out> sum([[=fut::from_path("a")]] long long a,
                             [[=fut::from_path("b")]] long long b);

// plain std::string return -> served as text/plain
[[=fut::http_get("/note")]]
asio::awaitable<std::string> note();

namespace beta {

// found by the recursive scan
[[=fut::http_get("/beta/note")]]
asio::awaitable<std::string> beta_note();

} // namespace beta

} // namespace web
