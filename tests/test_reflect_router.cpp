#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio.hpp>
#include <fut/annotate.hpp>
#include <fut/json.hpp>
#include <fut/refl_router.hpp>
#include <fut/route.hpp>

namespace asio = boost::asio;

namespace {

struct ping_dto { bool pong{true}; std::string app{"demo"}; };
struct add_in { int a{}; int b{}; };
struct add_out { int result{}; };
struct greet_out { std::string hi{}; };
struct mul_out { int result{}; };
struct raw_out { std::string method{}; std::string target{}; };
struct who_dto { std::string app{"demo"}; int n{3}; };

// ---- actions: one per binding style ----

[[=fut::http_get("/ping")]]
asio::awaitable<ping_dto> ping() { co_return ping_dto{}; }

[[=fut::http_post("/add")]]
asio::awaitable<add_out> add([[=fut::from_body{}]] add_in in) { co_return add_out{in.a + in.b}; }

// empty key -> parameter name ("name")
[[=fut::http_get("/greet")]]
asio::awaitable<greet_out> greet([[=fut::from_query{}]] std::string name) {
    co_return greet_out{"hi " + name};
}

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

// string return + path placeholder binding together
[[=fut::http_get("/mix/{n}")]]
asio::awaitable<std::string> mixed([[=fut::from_path("n")]] int n) {
    co_return "n squared is " + std::to_string(n * n);
}

constexpr auto table = fut::refl::make_app_table<^^ping, ^^add, ^^greet, ^^mul, ^^raw,
                                                  ^^who, ^^txt, ^^mixed>("demo", "/api/demo");

// ---- namespace-scanned table (recursion included) ----

namespace scan_demo {

[[=fut::http_get("/one")]]
inline asio::awaitable<greet_out> scan_one() { co_return greet_out{"flat"}; }

namespace nested {
[[=fut::http_get("/two")]]
inline asio::awaitable<greet_out> scan_two() { co_return greet_out{"nested"}; }
} // namespace nested

// a controller in the same namespace: its methods join the same table
struct [[=fut::controller("/grp")]] scan_controller {
    [[=fut::http_get("/first")]]
    asio::awaitable<greet_out> scan_ctrl() { co_return greet_out{"controller"}; }
};

} // namespace scan_demo

constexpr auto scanned =
    fut::refl::make_scanned_table<^^scan_demo>("scan", "/api/scan");

// Drives the table exactly the way the server does.
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

fut::response run(fut::request req) { return run(table, std::move(req)); }

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

TEST_SUITE("annotation router over free functions") {

TEST_CASE("compile-time table") {
    CHECK(table.name == "demo");
    CHECK(table.prefix == "/api/demo");
    REQUIRE(table.entries().size() == 8);

    bool has_add = false, has_mul = false, has_txt = false, has_mix = false;
    for (const auto& r : table.entries()) {
        if (r.method == "POST" && r.pattern == "/add") has_add = true;
        if (r.method == "GET" && r.pattern == "/mul/{x}/{y}") has_mul = true;
        if (r.method == "GET" && r.pattern == "/txt") has_txt = true;
        if (r.method == "GET" && r.pattern == "/mix/{n}") has_mix = true;
    }
    CHECK(has_add);
    CHECK(has_mul);
    CHECK(has_txt);
    CHECK(has_mix);
}

TEST_CASE("GET /ping -- DTO return auto-serialization") {
    const auto res = run(make_req("GET", "/ping"));
    CHECK(res.status == 200);
    CHECK(res.content_type == "application/json");
    const auto v = fut::parse<ping_dto>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->pong == true);
    CHECK(v->app == "demo");
}

TEST_CASE("POST /add -- body DTO binding") {
    const auto res = run(make_req("POST", "/add", R"({"a":2,"b":5})"));
    CHECK(res.status == 200);
    const auto v = fut::parse<add_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 7);
}

TEST_CASE("POST /add -- invalid JSON -> 400") {
    CHECK(run(make_req("POST", "/add", "not-json")).status == 400);
}

TEST_CASE("GET /greet -- query binding, empty key uses parameter name") {
    const auto res = run(make_req("GET", "/greet", "", "name=fut"));
    CHECK(res.status == 200);
    const auto v = fut::parse<greet_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->hi == "hi fut");
}

TEST_CASE("GET /greet -- missing query parameter -> 400") {
    CHECK(run(make_req("GET", "/greet")).status == 400);
}

TEST_CASE("GET /mul/6/7 -- path placeholder binding") {
    const auto res = run(make_req("GET", "/mul/6/7"));
    CHECK(res.status == 200);
    const auto v = fut::parse<mul_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 42);
}

TEST_CASE("GET /mul/6/xx -- non-numeric segment -> 400") {
    CHECK(run(make_req("GET", "/mul/6/xx")).status == 400);
}

TEST_CASE("GET /raw -- const request& escape hatch") {
    const auto res = run(make_req("GET", "/raw"));
    CHECK(res.status == 200);
    const auto v = fut::parse<raw_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->method == "GET");
    CHECK(v->target == "/raw");
}

TEST_CASE("GET /txt -- plain string return served as text/plain") {
    const auto res = run(make_req("GET", "/txt"));
    CHECK(res.status == 200);
    CHECK(res.content_type.rfind("text/plain", 0) == 0);
    CHECK(res.body == "plain text from fut");
}

TEST_CASE("GET /mix/9 -- string return combined with path binding") {
    const auto res = run(make_req("GET", "/mix/9"));
    CHECK(res.status == 200);
    CHECK(res.body == "n squared is 81");
}

TEST_CASE("no match -> 404 (including method mismatch)") {
    CHECK(run(make_req("GET", "/nope")).status == 404);
    CHECK(run(make_req("POST", "/ping")).status == 404);
}

TEST_CASE("namespace scan discovers flat, nested and controller routes") {
    REQUIRE(scanned.route_count == 3);

    bool has_one = false, has_two = false, has_ctrl = false;
    for (const auto& r : scanned.entries()) {
        if (r.pattern == "/one") has_one = true;
        if (r.pattern == "/two") has_two = true;
        if (r.pattern == "/grp/first") has_ctrl = true;   // controller prefix joined
    }
    CHECK(has_one);
    CHECK(has_two);
    CHECK(has_ctrl);

    const auto res1 = run(scanned, make_req("GET", "/one"));
    const auto v1 = fut::parse<greet_out>(res1.body);
    REQUIRE(v1.has_value());
    CHECK(v1->hi == "flat");

    const auto res2 = run(scanned, make_req("GET", "/two"));
    const auto v2 = fut::parse<greet_out>(res2.body);
    REQUIRE(v2.has_value());
    CHECK(v2->hi == "nested");

    const auto res3 = run(scanned, make_req("GET", "/grp/first"));
    const auto v3 = fut::parse<greet_out>(res3.body);
    REQUIRE(v3.has_value());
    CHECK(v3->hi == "controller");
}

} // TEST_SUITE
