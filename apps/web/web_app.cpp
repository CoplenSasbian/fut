// /api/web —— 面向前端的业务 app（动态库 fut_app_web.dll）。
// 注解即路由 + 参数自动绑定。
//
// 路由：
//   GET  /api/web/ping             存活探测
//   GET  /api/web/hello?name=x     query 绑定（key 省略时用参数名）
//   GET  /api/web/time             服务器时间
//   POST /api/web/calc             请求体 DTO 绑定
//   GET  /api/web/sum/{a}/{b}      路径占位符绑定

#include <chrono>
#include <string>

#include <fut/annotate.hpp>
#include <fut/app_entry.hpp>
#include <fut/json.hpp>
#include <fut/refl_router.hpp>

namespace asio = boost::asio;

namespace {

struct calc_in {
    double a{};
    double b{};
    std::string op{};
};

struct calc_out {
    double result{};
    std::string op{};
};

struct time_out {
    long long epoch_seconds{};
};

struct sum_out {
    long long a{};
    long long b{};
    long long sum{};
};

class web_app final : public fut::app {
public:
    std::string_view name() const override { return "web"; }
    std::string_view prefix() const override { return "/api/web"; }

    asio::awaitable<fut::response> handle(const fut::request& req) override {
        co_return co_await fut::refl::dispatch(*this, req);
    }

    [[=fut::http_get("/ping")]]
    asio::awaitable<fut::response> ping() {
        struct pong { bool pong{true}; std::string app{"web"}; };
        co_return fut::json(pong{});
    }

    // key 省略（from_query{}）-> 用参数名 "name"
    [[=fut::http_get("/hello")]]
    asio::awaitable<fut::response> hello([[=fut::from_query{}]] std::string name) {
        struct hello_out { std::string hello; };
        co_return fut::json(hello_out{"hello, " + name});
    }

    [[=fut::http_get("/time")]]
    asio::awaitable<time_out> time() {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        co_return time_out{std::chrono::duration_cast<std::chrono::seconds>(now).count()};
    }

    [[=fut::http_post("/calc")]]
    asio::awaitable<fut::response> calc([[=fut::from_body{}]] calc_in in) {
        if (in.op == "add") co_return fut::json(calc_out{in.a + in.b, in.op});
        if (in.op == "sub") co_return fut::json(calc_out{in.a - in.b, in.op});
        if (in.op == "mul") co_return fut::json(calc_out{in.a * in.b, in.op});
        if (in.op == "div") {
            if (in.b == 0.0) co_return fut::error(400, "division by zero");
            co_return fut::json(calc_out{in.a / in.b, in.op});
        }
        co_return fut::error(400, "unknown op: " + in.op);
    }

    // 路径占位符："/sum/{a}/{b}" 的段绑定到参数
    [[=fut::http_get("/sum/{a}/{b}")]]
    asio::awaitable<fut::response> sum([[=fut::from_path("a")]] long long a,
                                       [[=fut::from_path("b")]] long long b) {
        co_return fut::json(sum_out{a, b, a + b});
    }
};

} // namespace

FUT_IMPLEMENT_APP(web_app)
