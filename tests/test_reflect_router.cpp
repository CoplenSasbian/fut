#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio.hpp>
#include <fut/annotate.hpp>
#include <fut/app.hpp>
#include <fut/json.hpp>
#include <fut/refl_router.hpp>

namespace asio = boost::asio;

namespace {

struct add_in { int a{}; int b{}; };
struct add_out { int result{}; };
struct greet_out { std::string hi{}; };
struct mul_out { int result{}; };
struct raw_out { std::string method{}; std::string target{}; };
struct who_dto { std::string app{"demo"}; int n{3}; };
struct ping_dto { bool pong{true}; std::string app{"demo"}; };

// 注解式路由的演示 app：覆盖全部绑定方式
class demo_app final : public fut::app {
public:
    std::string_view name() const override { return "demo"; }
    std::string_view prefix() const override { return "/api/demo"; }

    asio::awaitable<fut::response> handle(const fut::request& req) override {
        co_return co_await fut::refl::dispatch(*this, req);
    }

    [[=fut::http_get("/ping")]]
    asio::awaitable<fut::response> ping() { co_return fut::json(ping_dto{}); }

    [[=fut::http_post("/add")]]
    asio::awaitable<fut::response> add([[=fut::from_body{}]] add_in in) {
        co_return fut::json(add_out{in.a + in.b});
    }

    // key 省略（from_query{}）-> 默认用参数名 "name"
    [[=fut::http_get("/greet")]]
    asio::awaitable<fut::response> greet([[=fut::from_query{}]] std::string name) {
        co_return fut::json(greet_out{"hi " + name});
    }

    // 路径占位符绑定
    [[=fut::http_get("/mul/{x}/{y}")]]
    asio::awaitable<fut::response> mul([[=fut::from_path("x")]] int x,
                                       [[=fut::from_path("y")]] int y) {
        co_return fut::json(mul_out{x * y});
    }

    // 无注解参数 = const fut::request& 逃生舱
    [[=fut::http_get("/raw")]]
    asio::awaitable<fut::response> raw(const fut::request& req) {
        co_return fut::json(raw_out{std::string(req.method), std::string(req.target)});
    }

    // 返回值 DTO：框架自动 glaze 序列化
    [[=fut::http_get("/who")]]
    asio::awaitable<who_dto> who() { co_return who_dto{}; }
};

struct run_result {
    fut::response res{};
    std::exception_ptr err{};
};

asio::awaitable<void> drive(run_result& rr, fut::app* a, fut::request req) {
    rr.res = co_await a->handle(req);
}

fut::response run_handle(fut::app& a, fut::request req) {
    asio::io_context ioc;
    run_result rr;
    asio::co_spawn(ioc, drive(rr, &a, std::move(req)),
                   [&rr](std::exception_ptr e) { rr.err = e; });
    ioc.run();
    if (rr.err) std::rethrow_exception(rr.err);
    return rr.res;
}

fut::request make_req(std::string_view method, std::string_view path,
                      std::string_view body = {}, std::string_view query = {}) {
    fut::request r;
    r.method = method;
    r.target = path;
    r.path = path;
    r.body = body;
    r.query = query;
    return r;
}

} // namespace

TEST_SUITE("C++26 注解路由") {

TEST_CASE("编译期路由表：注解函数被 std::meta 收集") {
    constexpr auto& table = fut::refl::route_table_v<demo_app>;
    CHECK(table.size() == 6);
    bool has_add = false, has_mul = false, has_who = false;
    for (const auto& r : table) {
        if (r.method == "POST" && r.pattern == "/add") has_add = true;
        if (r.method == "GET" && r.pattern == "/mul/{x}/{y}") has_mul = true;
        if (r.method == "GET" && r.pattern == "/who") has_who = true;
    }
    CHECK(has_add);
    CHECK(has_mul);
    CHECK(has_who);
}

TEST_CASE("GET /ping") {
    demo_app app;
    const auto res = run_handle(app, make_req("GET", "/ping"));
    CHECK(res.status == 200);
    const auto v = fut::parse<ping_dto>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->pong == true);
    CHECK(v->app == "demo");
}

TEST_CASE("POST /add：请求体 DTO 绑定") {
    demo_app app;
    const auto res = run_handle(app, make_req("POST", "/add", R"({"a":2,"b":5})"));
    CHECK(res.status == 200);
    const auto v = fut::parse<add_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 7);
}

TEST_CASE("POST /add：非法 JSON -> 400") {
    demo_app app;
    CHECK(run_handle(app, make_req("POST", "/add", "not-json")).status == 400);
}

TEST_CASE("GET /greet?name=fut：query 绑定（key 默认取参数名）") {
    demo_app app;
    fut::request req = make_req("GET", "/greet");
    req.query = "name=fut";
    const auto res = run_handle(app, req);
    CHECK(res.status == 200);
    const auto v = fut::parse<greet_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->hi == "hi fut");
}

TEST_CASE("GET /greet：缺 query 参数 -> 400") {
    demo_app app;
    CHECK(run_handle(app, make_req("GET", "/greet")).status == 400);
}

TEST_CASE("GET /mul/6/7：路径占位符绑定") {
    demo_app app;
    const auto res = run_handle(app, make_req("GET", "/mul/6/7"));
    CHECK(res.status == 200);
    const auto v = fut::parse<mul_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 42);
}

TEST_CASE("GET /mul/6/xx：非数字段 -> 400") {
    demo_app app;
    CHECK(run_handle(app, make_req("GET", "/mul/6/xx")).status == 400);
}

TEST_CASE("GET /raw：const request& 逃生舱") {
    demo_app app;
    const auto res = run_handle(app, make_req("GET", "/raw"));
    CHECK(res.status == 200);
    const auto v = fut::parse<raw_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->method == "GET");
    CHECK(v->target == "/raw");
}

TEST_CASE("GET /who：返回值 DTO 自动序列化") {
    demo_app app;
    const auto res = run_handle(app, make_req("GET", "/who"));
    CHECK(res.status == 200);
    const auto v = fut::parse<who_dto>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->app == "demo");
    CHECK(v->n == 3);
}

TEST_CASE("未命中 -> 404（含方法不匹配）") {
    demo_app app;
    CHECK(run_handle(app, make_req("GET", "/nope")).status == 404);
    CHECK(run_handle(app, make_req("POST", "/ping")).status == 404);
}

} // TEST_SUITE
