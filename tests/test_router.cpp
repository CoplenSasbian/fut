#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string_view>

#include <fut/app.hpp>
#include "router.hpp"

namespace asio = boost::asio;

namespace {

class stub_app final : public fut::app {
public:
    explicit stub_app(std::string_view prefix) : prefix_(prefix) {}
    std::string_view name() const override { return "stub"; }
    std::string_view prefix() const override { return prefix_; }
    asio::awaitable<fut::response> handle(const fut::request&) override { co_return fut::response{}; }
private:
    std::string_view prefix_;
};

} // namespace

TEST_CASE("前缀精确与子路径匹配") {
    fut::exec::router rt;
    const stub_app admin("/api/admin");
    const stub_app web("/api/web");
    rt.add(const_cast<stub_app*>(&admin));
    rt.add(const_cast<stub_app*>(&web));

    auto m = rt.route("/api/admin/ping");
    CHECK(m.app == &admin);
    CHECK(m.rel_path == "/ping");

    m = rt.route("/api/admin");
    CHECK(m.app == &admin);
    CHECK(m.rel_path == "/");

    m = rt.route("/api/web/hello?name=fut&x=1");
    CHECK(m.app == &web);
    CHECK(m.rel_path == "/hello");
    CHECK(m.query == "name=fut&x=1");

    m = rt.route("/api/nothing");
    CHECK(m.app == nullptr);

    // 不允许半截匹配
    m = rt.route("/api/adminx");
    CHECK(m.app == nullptr);
}

TEST_CASE("更长前缀优先") {
    fut::exec::router rt;
    const stub_app api("/api");
    const stub_app admin("/api/admin");
    rt.add(const_cast<stub_app*>(&api));
    rt.add(const_cast<stub_app*>(&admin));

    auto m = rt.route("/api/admin/ping");
    CHECK(m.app == &admin);
    m = rt.route("/api/other");
    CHECK(m.app == &api);
    CHECK(m.rel_path == "/other");
}

TEST_CASE("空与非法前缀不参与匹配") {
    fut::exec::router rt;
    const stub_app bad("no-slash");
    rt.add(const_cast<stub_app*>(&bad));
    CHECK(rt.route("/no-slash").app == nullptr);
    CHECK(rt.route("/").app == nullptr);
    CHECK(rt.apps().size() == 1);
}
