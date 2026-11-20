#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio.hpp>
#include <fut/json.hpp>
#include <fut/http/route.hpp>

#include "app_loader.hpp"

namespace asio = boost::asio;

namespace {

// Dispatches a request against a loaded app table exactly like the server.
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

TEST_SUITE("app DLL end-to-end") {

TEST_CASE("load fut_app_admin / fut_app_web and drive their tables") {
    const char* dir = std::getenv("FUT_APPS_DIR");
    REQUIRE(dir != nullptr);

    fut::exec::app_loader loader;
    const auto apps = loader.load_dir(dir);
    REQUIRE(apps.size() == 2);

    const fut::app_table* admin = nullptr;
    const fut::app_table* web = nullptr;
    for (const auto& e : apps) {
        if (e.table->name == "admin") admin = e.table;
        if (e.table->name == "web") web = e.table;
    }
    REQUIRE(admin != nullptr);
    REQUIRE(web != nullptr);
    CHECK(admin->prefix == "/api/admin");
    CHECK(web->prefix == "/api/web");
    CHECK(admin->route_count == 8);  // 6 flat actions + 2 inside users_controller
    CHECK(web->route_count == 7);    // 6 flat actions + 1 inside namespace web::beta

    SUBCASE("admin /ping") {
        struct pong { bool pong{}; std::string app{}; };
        const auto res = run(*admin, make_req("GET", "/ping"));
        CHECK(res.status == 200);
        const auto v = fut::parse<pong>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->pong == true);
        CHECK(v->app == "admin");
    }

    SUBCASE("admin /echo -- body roundtrip") {
        struct echo_out { std::string message{}; int doubled{}; long long sum{}; };
        const auto res = run(*admin, make_req("POST", "/echo", R"({"message":"hi","count":4})"));
        CHECK(res.status == 200);
        const auto v = fut::parse<echo_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->message == "hi");
        CHECK(v->doubled == 8);
        CHECK(v->sum == 10);
    }

    SUBCASE("admin /echo -- invalid JSON -> 400") {
        CHECK(run(*admin, make_req("POST", "/echo", "not-json")).status == 400);
    }

    SUBCASE("admin /meta -- C++26 std::meta member enumeration") {
        struct member_row { std::string name{}; std::string value_kind{}; };
        struct meta_dto { std::vector<member_row> members{}; std::vector<std::string> names{}; };
        const auto res = run(*admin, make_req("GET", "/meta"));
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

    SUBCASE("admin /whoami -- DTO return auto-serialization") {
        struct who { std::string app{}; std::string user{}; };
        const auto res = run(*admin, make_req("GET", "/whoami"));
        CHECK(res.status == 200);
        const auto v = fut::parse<who>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->app == "admin");
        CHECK(v->user == "root");
    }

    SUBCASE("admin /raw -- string action served as text/plain") {
        const auto res = run(*admin, make_req("GET", "/raw"));
        CHECK(res.status == 200);
        CHECK(res.content_type.rfind("text/plain", 0) == 0);
        CHECK(res.body.find("fut admin app") == 0);
    }

    SUBCASE("web /hello?name=fut -- query binding") {
        struct hello { std::string hello{}; };
        const auto res = run(*web, make_req("GET", "/hello", "", "name=fut"));
        CHECK(res.status == 200);
        const auto v = fut::parse<hello>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->hello == "hello, fut");
    }

    SUBCASE("web /calc -- body DTO binding") {
        struct calc_out { double result{}; std::string op{}; };
        const auto res = run(*web, make_req("POST", "/calc", R"({"a":6,"b":7,"op":"mul"})"));
        CHECK(res.status == 200);
        const auto v = fut::parse<calc_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->result == doctest::Approx(42.0));
        CHECK(v->op == "mul");
    }

    SUBCASE("web /calc -- division by zero -> 400") {
        CHECK(run(*web, make_req("POST", "/calc", R"({"a":6,"b":0,"op":"div"})")).status == 400);
    }

    SUBCASE("web /sum/6/7 -- path placeholder binding") {
        struct sum_out { long long a{}; long long b{}; long long sum{}; };
        const auto res = run(*web, make_req("GET", "/sum/6/7"));
        CHECK(res.status == 200);
        const auto v = fut::parse<sum_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->sum == 13);
    }

    SUBCASE("admin /users/hits -- controller instance state") {
        struct hits_out { std::uint64_t hits{}; };
        const auto r1 = run(*admin, make_req("GET", "/users/hits"));
        CHECK(r1.status == 200);
        const auto v1 = fut::parse<hits_out>(r1.body);
        REQUIRE(v1.has_value());
        const auto r2 = run(*admin, make_req("GET", "/users/hits"));
        const auto v2 = fut::parse<hits_out>(r2.body);
        REQUIRE(v2.has_value());
        // one instance per DLL: the counter advances between calls
        CHECK(v2->hits == v1->hits + 1);
    }

    SUBCASE("admin /users/7/name -- controller prefix + path placeholder") {
        struct name_out { long long id{}; std::string name{}; };
        const auto res = run(*admin, make_req("GET", "/users/7/name"));
        CHECK(res.status == 200);
        const auto v = fut::parse<name_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->id == 7);
        CHECK(v->name == "user-7");
    }

    SUBCASE("web /note -- string action") {
        const auto res = run(*web, make_req("GET", "/note"));
        CHECK(res.status == 200);
        CHECK(res.content_type.rfind("text/plain", 0) == 0);
        CHECK(res.body.find("fut web app") == 0);
    }

    SUBCASE("web /beta/note -- action in a nested namespace") {
        const auto res = run(*web, make_req("GET", "/beta/note"));
        CHECK(res.status == 200);
        CHECK(res.content_type.rfind("text/plain", 0) == 0);
        CHECK(res.body == "from namespace web::beta");
    }

    SUBCASE("unknown route -> 404") {
        CHECK(run(*admin, make_req("GET", "/nope")).status == 404);
        CHECK(run(*web, make_req("GET", "/nope")).status == 404);
    }
}

} // TEST_SUITE
