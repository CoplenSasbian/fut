#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string_view>

#include <boost/asio/awaitable.hpp>

#include <fut/http/router.hpp>

#include "router.hpp"

namespace asio = boost::asio;

namespace {

asio::awaitable<fut::response> dummy(const fut::request&, const fut::path_match&) {
    co_return fut::response{};
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
