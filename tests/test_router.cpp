#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio.hpp>

#include <fut/http/router.hpp>
#include <fut/json.hpp>

#include "router.hpp"

namespace asio = boost::asio;

namespace {

asio::awaitable<fut::response> dummy(const fut::request&, const fut::path_match&) {
    fut::response res;
    res.body = "handler";
    co_return res;
}

struct run_state {
    fut::response res{};
    std::exception_ptr err{};
};

asio::awaitable<void> invoke_into(run_state& st, const fut::exec::router& rt, fut::request req) {
    try {
        st.res = co_await rt.dispatch(req);
    } catch (...) {
        st.err = std::current_exception();
    }
}

fut::response run(const fut::exec::router& rt, std::string_view method, std::string_view path,
                  std::span<const std::pair<std::string_view, std::string_view>> headers = {}) {
    asio::io_context ioc;
    run_state st;
    asio::co_spawn(ioc, invoke_into(st, rt, fut::request{method, path, path, {}, {}, headers}),
                   [](std::exception_ptr) {});
    ioc.run();
    if (st.err) std::rethrow_exception(st.err);
    return st.res;
}

bool has_header(const fut::response& res, std::string_view name, std::string_view value) {
    for (const auto& [k, v] : res.headers)
        if (k == name && v == value) return true;
    return false;
}

} // namespace

TEST_CASE("routes are matched by their full pattern") {
    fut::exec::router rt;
    rt.begin_app("admin", "/api/admin");
    rt.add_route(fut::method::GET, "/api/admin/ping", &dummy);
    rt.add_route(fut::method::GET, "/api/admin/users/{id}/name", &dummy);
    rt.end_app();

    const auto ping = rt.match(fut::method::GET, "/api/admin/ping");
    REQUIRE(ping.has_value());
    CHECK(ping->route->pattern == "/api/admin/ping");

    const auto byId = rt.match(fut::method::GET, "/api/admin/users/7/name");
    REQUIRE(byId.has_value());
    CHECK(byId->params.segment("id") == "7");

    CHECK_FALSE(rt.match(fut::method::GET, "/api/admin/nope").has_value());
    CHECK_FALSE(rt.match(fut::method::POST, "/api/admin/ping").has_value());
}

TEST_CASE("app bookkeeping while registering") {
    fut::exec::router rt;
    rt.begin_app("admin", "/api/admin");
    rt.add_route(fut::method::GET, "/api/admin/ping", &dummy);
    rt.add_route(fut::method::POST, "/api/admin/echo", &dummy);
    rt.end_app();

    CHECK(rt.apps().size() == 1);
    CHECK(rt.apps()[0].name == "admin");
    CHECK(rt.apps()[0].prefix == "/api/admin");
    CHECK(rt.apps()[0].routes == 2);
    REQUIRE(rt.entries().size() == 2);
    CHECK(rt.entries()[0].app == "admin");
}

TEST_CASE("routes added outside an app carry no app name") {
    fut::exec::router rt;
    rt.add_route(fut::method::GET, "/standalone", &dummy);
    REQUIRE(rt.entries().size() == 1);
    CHECK(rt.entries()[0].app.empty());
    CHECK(rt.match(fut::method::GET, "/standalone").has_value());
}

TEST_CASE("platform middleware wraps matched routes and 404s") {
    fut::exec::router rt;
    rt.begin_app("x", "/x");
    rt.add_route(fut::method::GET, "/x/ping", &dummy);
    rt.end_app();

    rt.add_global_middleware([](const fut::request& req, const fut::path_match& params,
                                const fut::http::next_fn& next)
                                 -> asio::awaitable<fut::response> {
        fut::response res = co_await next(req, params);
        res.headers.emplace_back("X-Platform", "1");
        co_return res;
    });
    rt.finalize();

    const auto ok = run(rt, "GET", "/x/ping");
    CHECK(ok.status == 200);
    CHECK(ok.body == "handler");
    CHECK(has_header(ok, "X-Platform", "1"));

    // no route, but the platform middleware still ran
    const auto missing = run(rt, "GET", "/x/nope");
    CHECK(missing.status == 404);
    CHECK(has_header(missing, "X-Platform", "1"));
}

TEST_CASE("a middleware can short-circuit and can rewrite the body") {
    fut::exec::router rt;
    rt.begin_app("x", "/x");
    rt.add_route(fut::method::GET, "/x/secret", &dummy);
    rt.end_app();

    rt.add_global_middleware([](const fut::request& req, const fut::path_match& params,
                                const fut::http::next_fn& next)
                                 -> asio::awaitable<fut::response> {
        if (req.header("x-key") != "ok") co_return fut::error(401, "blocked");
        fut::response res = co_await next(req, params);
        res.body += "+rewritten";
        co_return res;
    });
    rt.finalize();

    CHECK(run(rt, "GET", "/x/secret").status == 401);

    const std::pair<std::string_view, std::string_view> headers[] = {{"x-key", "ok"}};
    const auto res = run(rt, "GET", "/x/secret", headers);
    CHECK(res.status == 200);
    CHECK(res.body == "handler+rewritten");
}

TEST_CASE("app middleware applies to its own routes only") {
    fut::exec::router rt;

    rt.begin_app("a", "/a");
    rt.add_route(fut::method::GET, "/a/ping", &dummy);
    rt.add_middleware([](const fut::request& req, const fut::path_match& params,
                         const fut::http::next_fn& next) -> asio::awaitable<fut::response> {
        fut::response res = co_await next(req, params);
        res.headers.emplace_back("X-App", "a");
        co_return res;
    });
    rt.end_app();

    rt.begin_app("b", "/b");
    rt.add_route(fut::method::GET, "/b/ping", &dummy);
    rt.end_app();
    rt.finalize();

    CHECK(has_header(run(rt, "GET", "/a/ping"), "X-App", "a"));
    CHECK_FALSE(has_header(run(rt, "GET", "/b/ping"), "X-App", "a"));
}

TEST_CASE("literal segments beat placeholders") {
    fut::exec::router rt;
    rt.begin_app("x", "/x");
    rt.add_route(fut::method::GET, "/users/{id}", &dummy);
    rt.add_route(fut::method::GET, "/users/hits", &dummy);
    rt.end_app();

    const auto hit = rt.match(fut::method::GET, "/users/hits");
    REQUIRE(hit.has_value());
    CHECK(hit->route->pattern == "/users/hits");

    const auto param = rt.match(fut::method::GET, "/users/42");
    REQUIRE(param.has_value());
    CHECK(param->route->pattern == "/users/{id}");
    CHECK(param->params.segment("id") == "42");
}
