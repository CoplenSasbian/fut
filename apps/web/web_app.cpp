// /api/web —— 面向前端的业务示例 app（动态库 fut_app_web.dll）。
// 路由：
//   GET  /api/web/ping            存活探测
//   GET  /api/web/hello?name=x    query 解析
//   GET  /api/web/time            服务器时间
//   POST /api/web/calc            JSON 计算 {"a":..,"b":..,"op":"add|sub|mul|div"}

#include <chrono>
#include <string>

#include <fut/app_entry.hpp>
#include <fut/json.hpp>
#include <fut/url.hpp>

namespace asio = boost::asio;

namespace {

class web_app final : public fut::app {
public:
    std::string_view name() const override { return "web"; }
    std::string_view prefix() const override { return "/api/web"; }

    asio::awaitable<fut::response> handle(const fut::request& req) override {
        if (req.path == "/ping") {
            struct pong { bool pong{true}; std::string app{"web"}; };
            co_return fut::json(pong{});
        }
        if (req.path == "/hello" && req.method == "GET") {
            std::string who = "world";
            for (const auto& [k, v] : fut::parse_query(req.query))
                if (k == "name") who = v;
            struct hello_out { std::string hello; };
            co_return fut::json(hello_out{"hello, " + who});
        }
        if (req.path == "/time" && req.method == "GET") {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            struct time_out { long long epoch_seconds; };
            co_return fut::json(time_out{
                std::chrono::duration_cast<std::chrono::seconds>(now).count()});
        }
        if (req.path == "/calc" && req.method == "POST") {
            struct calc_in { double a{}; double b{}; std::string op{}; };
            struct calc_out { double result{}; std::string op{}; };
            auto in = fut::parse<calc_in>(req.body);
            if (!in) co_return fut::error(400, R"(expected JSON body: {"a":1,"b":2,"op":"add|sub|mul|div"})");
            if (in->op == "add") co_return fut::json(calc_out{in->a + in->b, in->op});
            if (in->op == "sub") co_return fut::json(calc_out{in->a - in->b, in->op});
            if (in->op == "mul") co_return fut::json(calc_out{in->a * in->b, in->op});
            if (in->op == "div") {
                if (in->b == 0.0) co_return fut::error(400, "division by zero");
                co_return fut::json(calc_out{in->a / in->b, in->op});
            }
            co_return fut::error(400, "unknown op: " + in->op);
        }
        co_return fut::error(404, "web: no such route");
    }
};

} // namespace

FUT_IMPLEMENT_APP(web_app)
