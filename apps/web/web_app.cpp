// /api/web -- sample front-end-facing app (fut_app_web.dll).
// Free-function actions, annotation routes, automatic parameter binding.

#include <chrono>
#include <string>

#include <fut/annotate.hpp>
#include <fut/json.hpp>
#include <fut/refl_router.hpp>

namespace asio = boost::asio;

namespace {

struct pong_dto { bool pong{true}; std::string app{"web"}; };
struct hello_out { std::string hello{}; };
struct time_out { long long epoch_seconds{}; };
struct banner_dto { std::string app{"web"}; std::string message{"hello from fut"}; };

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

// ---- actions ----

[[=fut::http_get("/ping")]]
asio::awaitable<pong_dto> ping() { co_return pong_dto{}; }

// empty key -> falls back to the parameter name ("name")
[[=fut::http_get("/hello")]]
asio::awaitable<hello_out> hello([[=fut::from_query{}]] std::string name) {
    co_return hello_out{"hello, " + name};
}

[[=fut::http_get("/time")]]
asio::awaitable<time_out> time() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    co_return time_out{std::chrono::duration_cast<std::chrono::seconds>(now).count()};
}

[[=fut::http_post("/calc")]]
asio::awaitable<calc_out> calc([[=fut::from_body{}]] calc_in in) {
    if (in.op == "add") co_return calc_out{in.a + in.b, in.op};
    if (in.op == "sub") co_return calc_out{in.a - in.b, in.op};
    if (in.op == "mul") co_return calc_out{in.a * in.b, in.op};
    if (in.op == "div") {
        if (in.b == 0.0) throw fut::bind_error("division by zero");
        co_return calc_out{in.a / in.b, in.op};
    }
    throw fut::bind_error("unknown op: " + in.op);
}

// path placeholder binding: "/sum/{a}/{b}"
[[=fut::http_get("/sum/{a}/{b}")]]
asio::awaitable<sum_out> sum([[=fut::from_path("a")]] long long a,
                             [[=fut::from_path("b")]] long long b) {
    co_return sum_out{a, b, a + b};
}

// plain std::string return -> served as text/plain
[[=fut::http_get("/note")]]
asio::awaitable<std::string> note() {
    co_return "fut web app -- string actions are served as plain text";
}

} // namespace

FUT_EXPORT_APP("web", "/api/web", ^^ping, ^^hello, ^^time, ^^calc, ^^sum, ^^note)
