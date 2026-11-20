#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string_view>

#include <boost/asio/awaitable.hpp>
#include <fut/route.hpp>
#include "router.hpp"

namespace asio = boost::asio;

namespace {

asio::awaitable<fut::response> dummy_invoke(const fut::request&, const fut::path_match&) {
    co_return fut::response{};
}

constexpr fut::route_record recs_admin[]{fut::route_record{"GET", "/ping", &dummy_invoke}};
constexpr fut::route_record recs_web[]{fut::route_record{"GET", "/hello", &dummy_invoke}};
constexpr fut::route_record recs_api[]{fut::route_record{"GET", "/other", &dummy_invoke}};

constexpr fut::app_table admin_t{"admin", "/api/admin", recs_admin, 1};
constexpr fut::app_table web_t{"web", "/api/web", recs_web, 1};
constexpr fut::app_table api_t{"api", "/api", recs_api, 1};
constexpr fut::app_table bad_t{"bad", "no-slash", nullptr, 0};

} // namespace

TEST_CASE("exact prefix and sub-paths") {
    fut::exec::router rt;
    rt.add(&admin_t);
    rt.add(&web_t);

    auto m = rt.route("/api/admin/ping");
    CHECK(m.app == &admin_t);
    CHECK(m.rel_path == "/ping");

    m = rt.route("/api/admin");
    CHECK(m.app == &admin_t);
    CHECK(m.rel_path == "/");

    m = rt.route("/api/web/hello?name=fut&x=1");
    CHECK(m.app == &web_t);
    CHECK(m.rel_path == "/hello");
    CHECK(m.query == "name=fut&x=1");

    m = rt.route("/api/nothing");
    CHECK(m.app == nullptr);
}

TEST_CASE("longer prefix wins") {
    fut::exec::router rt;
    rt.add(&api_t);
    rt.add(&admin_t);

    auto m = rt.route("/api/admin/ping");
    CHECK(m.app == &admin_t);

    m = rt.route("/api/other");
    CHECK(m.app == &api_t);
    CHECK(m.rel_path == "/other");
}

TEST_CASE("malformed prefixes never match") {
    fut::exec::router rt;
    rt.add(&bad_t);
    CHECK(rt.route("/no-slash").app == nullptr);
    CHECK(rt.route("/").app == nullptr);
    CHECK(rt.apps().size() == 1);
}
