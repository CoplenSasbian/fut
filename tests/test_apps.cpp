#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include <boost/asio.hpp>
#include <fut/json.hpp>
#include <fut/reflect.hpp>

#include "app_loader.hpp"

namespace asio = boost::asio;

namespace {

struct run_result {
    fut::response res{};
    std::exception_ptr err{};
};

asio::awaitable<void> drive(run_result& rr, fut::app* a, fut::request req) {
    rr.res = co_await a->handle(req);
}

fut::response run_handle(fut::app* a, fut::request req) {
    asio::io_context ioc;
    run_result rr;
    asio::co_spawn(ioc, drive(rr, a, std::move(req)),
                   [&rr](std::exception_ptr e) { rr.err = e; });
    ioc.run();
    if (rr.err) std::rethrow_exception(rr.err);
    return rr.res;
}

fut::request make_req(std::string_view method, std::string_view path,
                      std::string_view body = {}) {
    fut::request r;
    r.method = method;
    r.target = path;
    r.path = path;
    r.body = body;
    return r;
}

} // namespace

TEST_SUITE("业务动态库全链路") {

TEST_CASE("加载 fut_app_admin / fut_app_web") {
    const char* dir = std::getenv("FUT_APPS_DIR");
    REQUIRE(dir != nullptr);

    fut::exec::app_loader loader;
    const auto apps = loader.load_dir(dir);
    REQUIRE(apps.size() == 2);

    fut::app* admin = nullptr;
    fut::app* web = nullptr;
    for (const auto& e : apps) {
        if (e.instance->name() == "admin") admin = e.instance;
        if (e.instance->name() == "web") web = e.instance;
    }
    REQUIRE(admin != nullptr);
    REQUIRE(web != nullptr);
    CHECK(admin->prefix() == "/api/admin");
    CHECK(web->prefix() == "/api/web");

    SUBCASE("admin /ping") {
        struct pong { bool pong{}; std::string app{}; };
        const auto res = run_handle(admin, make_req("GET", "/ping"));
        CHECK(res.status == 200);
        const auto v = fut::parse<pong>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->pong == true);
        CHECK(v->app == "admin");
    }

    SUBCASE("admin /echo JSON 往返") {
        struct echo_out { std::string message{}; int doubled{}; long long sum{}; };
        const auto res = run_handle(admin, make_req("POST", "/echo", R"({"message":"hi","count":4})"));
        CHECK(res.status == 200);
        const auto v = fut::parse<echo_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->message == "hi");
        CHECK(v->doubled == 8);
        CHECK(v->sum == 10);
    }

    SUBCASE("admin /echo 非法 JSON -> 400") {
        const auto res = run_handle(admin, make_req("POST", "/echo", "not-json"));
        CHECK(res.status == 400);
    }

    SUBCASE("admin /meta C++26 反射") {
        struct member_row { std::string name{}; std::string value_kind{}; };
        struct meta_dto { std::vector<member_row> members{}; std::vector<std::string> names{}; };
        const auto res = run_handle(admin, make_req("GET", "/meta"));
        CHECK(res.status == 200);
        const auto v = fut::parse<meta_dto>(res.body);
        REQUIRE(v.has_value());
        REQUIRE(v->names.size() == 4);
        CHECK(v->names[0] == "name");
        CHECK(v->names[1] == "rank");
        CHECK(v->names[2] == "active");
        CHECK(v->names[3] == "tags");
        REQUIRE(v->members.size() == 4);
        CHECK(v->members[0].value_kind == "string");
        CHECK(v->members[1].value_kind == "int");
        CHECK(v->members[2].value_kind == "bool");
        CHECK(v->members[3].value_kind == "string[]");
    }

    SUBCASE("web /hello?name=fut") {
        struct hello { std::string hello{}; };
        fut::request req = make_req("GET", "/hello");
        req.query = "name=fut";
        const auto res = run_handle(web, req);
        CHECK(res.status == 200);
        const auto v = fut::parse<hello>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->hello == "hello, fut");
    }

    SUBCASE("web /calc 乘法") {
        struct calc_out { double result{}; std::string op{}; };
        const auto res = run_handle(web, make_req("POST", "/calc", R"({"a":6,"b":7,"op":"mul"})"));
        CHECK(res.status == 200);
        const auto v = fut::parse<calc_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->result == doctest::Approx(42.0));
        CHECK(v->op == "mul");
    }

    SUBCASE("web /calc 除零 -> 400") {
        const auto res = run_handle(web, make_req("POST", "/calc", R"({"a":6,"b":0,"op":"div"})"));
        CHECK(res.status == 400);
    }

    SUBCASE("未知路由 -> 404") {
        CHECK(run_handle(admin, make_req("GET", "/nope")).status == 404);
        CHECK(run_handle(web, make_req("GET", "/nope")).status == 404);
    }
}

} // TEST_SUITE
