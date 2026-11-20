#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio.hpp>
#include <fut/http/http.hpp>
#include <fut/json.hpp>


namespace asio = boost::asio;

// ---- test DTOs (at file scope, used by both controllers) ----

struct ping_dto { bool pong{true}; std::string app{"demo"}; };
struct add_in { int a{}; int b{}; };
struct add_out { int result{}; };
struct greet_out { std::string hi{}; };
struct mul_out { int result{}; };
struct raw_out { std::string method{}; std::string target{}; };
struct who_dto { std::string app{"demo"}; int n{3}; };

// ---- test namespace: controllers with every binding style ----

namespace demo_actions {

struct [[=fut::controller{}]] main_controller {
    [[=fut::http_get("/ping")]]
    asio::awaitable<ping_dto> ping() { co_return ping_dto{}; }

    [[=fut::http_post("/add")]]
    asio::awaitable<add_out> add([[=fut::from_body{}]] add_in in) {
        co_return add_out{in.a + in.b};
    }

    // empty key -> parameter name ("name")
    [[=fut::http_get("/greet")]]
    asio::awaitable<greet_out> greet([[=fut::from_query{}]] std::string name) {
        co_return greet_out{"hi " + name};
    }

    // path placeholder binding
    [[=fut::http_get("/mul/{x}/{y}")]]
    asio::awaitable<mul_out> mul([[=fut::from_path("x")]] int x,
                                 [[=fut::from_path("y")]] int y) {
        co_return mul_out{x * y};
    }

    // escape hatch: no annotation, const fut::request&
    [[=fut::http_get("/raw")]]
    asio::awaitable<raw_out> raw(const fut::request& req) {
        co_return raw_out{std::string(req.method), std::string(req.target)};
    }

    [[=fut::http_get("/who")]]
    asio::awaitable<who_dto> who() { co_return who_dto{}; }

    // plain string return -> text/plain body
    [[=fut::http_get("/txt")]]
    asio::awaitable<std::string> txt() { co_return "plain text from fut"; }

    // string return + path binding
    [[=fut::http_get("/mix/{n}")]]
    asio::awaitable<std::string> mixed([[=fut::from_path("n")]] int n) {
        co_return "n squared is " + std::to_string(n * n);
    }
};

struct [[=fut::controller("/grp")]] grouped_controller {
    [[=fut::http_get("/first")]]
    asio::awaitable<greet_out> first() { co_return greet_out{"grouped"}; }
};

namespace nested {
struct [[=fut::controller("/deep")]] nested_controller {
    [[=fut::http_get("/one")]]
    asio::awaitable<greet_out> deep_one() { co_return greet_out{"deep"}; }
};
} // namespace nested

} // namespace demo_actions

// ---- the scanned table ----

constexpr auto table =
    fut::refl::make_scanned_table<^^demo_actions>("demo", "/api/demo");

// ---- test helpers (anonymous namespace, not scanned) ----

namespace {

asio::awaitable<fut::response> call_table(const fut::app_table& t, fut::request req) {
    for (const fut::route_record& r : t.entries()) {
        if (r.method != req.method) continue;
        fut::path_match pm;
        if (!fut::match_pattern(r.pattern, req.path, pm)) continue;
        try {
            co_return co_await r.invoke(req, pm);
        } catch (const fut::bind_error& e) {
            co_return fut::error(400, e.what());
        }
    }
    co_return fut::error(404, "no route");
}

struct run_result {
    fut::response res{};
    std::exception_ptr err{};
};

asio::awaitable<void> drive(run_result& rr, const fut::app_table* t, fut::request req) {
    rr.res = co_await call_table(*t, std::move(req));
}

fut::response run(const fut::app_table& t, fut::request req) {
    asio::io_context ioc;
    run_result rr;
    asio::co_spawn(ioc, drive(rr, &t, std::move(req)),
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

TEST_SUITE("annotation router") {

TEST_CASE("compile-time table") {
    CHECK(table.name == "demo");
    CHECK(table.prefix == "/api/demo");
    REQUIRE(table.entries().size() == 10);   // 8 main + 1 grouped + 1 nested

    bool has_add = false, has_mul = false, has_txt = false, has_grp = false, has_deep = false;
    for (const auto& r : table.entries()) {
        if (r.method == "POST" && r.pattern == "/add") has_add = true;
        if (r.method == "GET" && r.pattern == "/mul/{x}/{y}") has_mul = true;
        if (r.method == "GET" && r.pattern == "/txt") has_txt = true;
        if (r.method == "GET" && r.pattern == "/grp/first") has_grp = true;
        if (r.method == "GET" && r.pattern == "/deep/one") has_deep = true;
    }
    CHECK(has_add);
    CHECK(has_mul);
    CHECK(has_txt);
    CHECK(has_grp);    // controller prefix joined
    CHECK(has_deep);   // nested namespace controller found
}

TEST_CASE("GET /ping") {
    const auto res = run(table, make_req("GET", "/ping"));
    CHECK(res.status == 200);
    const auto v = fut::parse<ping_dto>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->pong == true);
    CHECK(v->app == "demo");
}

TEST_CASE("POST /add") {
    const auto res = run(table, make_req("POST", "/add", R"({"a":2,"b":5})"));
    CHECK(res.status == 200);
    const auto v = fut::parse<add_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 7);
}

TEST_CASE("POST /add -- invalid JSON -> 400") {
    CHECK(run(table, make_req("POST", "/add", "not-json")).status == 400);
}

TEST_CASE("GET /greet -- query binding, empty key uses parameter name") {
    const auto res = run(table, make_req("GET", "/greet", "", "name=fut"));
    CHECK(res.status == 200);
    const auto v = fut::parse<greet_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->hi == "hi fut");
}

TEST_CASE("GET /greet -- missing query -> 400") {
    CHECK(run(table, make_req("GET", "/greet")).status == 400);
}

TEST_CASE("GET /mul/6/7 -- path placeholder binding") {
    const auto res = run(table, make_req("GET", "/mul/6/7"));
    CHECK(res.status == 200);
    const auto v = fut::parse<mul_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 42);
}

TEST_CASE("GET /mul/6/xx -- non-numeric -> 400") {
    CHECK(run(table, make_req("GET", "/mul/6/xx")).status == 400);
}

TEST_CASE("GET /raw -- const request& escape hatch") {
    const auto res = run(table, make_req("GET", "/raw"));
    CHECK(res.status == 200);
    const auto v = fut::parse<raw_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->method == "GET");
    CHECK(v->target == "/raw");
}

TEST_CASE("GET /txt -- plain string return") {
    const auto res = run(table, make_req("GET", "/txt"));
    CHECK(res.status == 200);
    CHECK(res.content_type.rfind("text/plain", 0) == 0);
    CHECK(res.body == "plain text from fut");
}

TEST_CASE("GET /mix/9 -- string + path binding") {
    const auto res = run(table, make_req("GET", "/mix/9"));
    CHECK(res.status == 200);
    CHECK(res.body == "n squared is 81");
}

TEST_CASE("GET /grp/first -- controller prefix joined") {
    const auto res = run(table, make_req("GET", "/grp/first"));
    CHECK(res.status == 200);
    const auto v = fut::parse<greet_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->hi == "grouped");
}

TEST_CASE("GET /deep/one -- nested namespace controller") {
    const auto res = run(table, make_req("GET", "/deep/one"));
    CHECK(res.status == 200);
    const auto v = fut::parse<greet_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->hi == "deep");
}

TEST_CASE("no match -> 404") {
    CHECK(run(table, make_req("GET", "/nope")).status == 404);
    CHECK(run(table, make_req("POST", "/ping")).status == 404);
}

} // TEST_SUITE
