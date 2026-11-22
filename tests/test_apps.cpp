#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include <fut/http/route.hpp>
#include <fut/json.hpp>
#include <fut/log.hpp>

#include "app_loader.hpp"
#include "module_registry.hpp"
#include "router.hpp"

namespace asio = boost::asio;

namespace {

struct run_state {
    fut::response res{};
    std::exception_ptr err{};
};

// The loop the tests drive requests on, exactly like the server owns one: it
// is registered as the "io" module, so an injected io_context* in an app
// points at the loop that runs the action.

asio::awaitable<void> invoke_into(run_state& st, const fut::exec::router& rt,
                                  fut::request req) {
    try {
        st.res = co_await rt.dispatch(req);
    } catch (...) {
        st.err = std::current_exception();
    }
}

// Dispatches exactly like the server: router::dispatch runs the middleware
// chains, matches, and maps errors (400/404/405/500).
fut::response run(asio::io_context& ioc, fut::exec::router& rt, fut::method m,
                  std::string_view path, std::string_view body = {},
                  std::string_view query = {},
                  std::span<const std::pair<std::string_view, std::string_view>> headers = {}) {
    run_state st;
    ioc.restart();   // run() stops the context when it drains
    asio::co_spawn(ioc,
                   invoke_into(st, rt,
                               fut::request{fut::to_string(m), path, path, query, body,
                                            headers}),
                   [](std::exception_ptr) {});
    ioc.run();
    if (st.err) std::rethrow_exception(st.err);
    return st.res;
}

// Captures what an app logs, so the test can prove the line arrived with the
// group and level the app chose.
struct logged_line {
    fut::log::level severity{fut::log::level::info};
    std::string group{};
    std::string message{};
};

std::vector<logged_line>& captured() {
    static std::vector<logged_line> lines;
    return lines;
}

void capture(void*, fut::log::level severity, std::string_view group,
             std::string_view message, std::string_view, std::string_view,
             std::uint32_t, std::uint32_t) {
    captured().push_back({severity, std::string(group), std::string(message)});
}

constexpr bool contains(const std::vector<logged_line>& lines, std::string_view group,
                        std::string_view message) {
    for (const auto& line : lines)
        if (line.group == group && line.message == message) return true;
    return false;
}

bool has_header(const fut::response& res, std::string_view name, std::string_view value) {
    for (const auto& [k, v] : res.headers)
        if (k == name && v == value) return true;
    return false;
}

const fut::exec::app_loader::entry* find_app(
    const std::vector<fut::exec::app_loader::entry>& apps, std::string_view name) {
    for (const auto& a : apps)
        if (a.name == name) return &a;
    return nullptr;
}

} // namespace

TEST_SUITE("app DLL end-to-end") {

TEST_CASE("load fut_app_admin / fut_app_web and drive their routes") {
    const char* dir = std::getenv("FUT_APPS_DIR");
    REQUIRE(dir != nullptr);

    // Declared first, destroyed last: the DLLs stay loaded until the router
    // and the io_context (which may hold DLL handlers) are gone.
    fut::exec::app_loader loader;
    asio::io_context ioc;   // the loop this test case owns
    fut::exec::router rt;
    fut::exec::module_registry modules;
    modules.add("io", typeid(asio::io_context).name(), &ioc);
    // registry stores void*; the service object itself is const by design
    static const fut::log::service log_service{nullptr, &capture};
    modules.add("log", typeid(fut::log::service).name(),
                const_cast<fut::log::service*>(&log_service));
    const auto apps = loader.load_dir(dir, rt, modules);
    REQUIRE(apps.size() == 2);

    // Platform middleware: belongs to the executable, sees every request.
    rt.add_global_middleware([](const fut::request& req, const fut::path_match& params,
                                const fut::http::next_fn& next)
                                 -> asio::awaitable<fut::response> {
        if (req.header("x-block") == "1") co_return fut::error(401, "blocked");
        fut::response res = co_await next(req, params);
        res.headers.emplace_back("X-Platform", "1");
        co_return res;
    });
    rt.finalize();   // wraps every route with its app's chain

    const auto* admin = find_app(apps, "admin");
    const auto* web = find_app(apps, "web");
    REQUIRE(admin != nullptr);
    REQUIRE(web != nullptr);
    CHECK(admin->prefix == "/api/admin");
    CHECK(web->prefix == "/api/web");
    CHECK(admin->routes == 13);  // 11 flat + 2 inside users_controller
    CHECK(web->routes == 7);    // 6 flat + 1 inside web::beta

    SUBCASE("admin /ping") {
        struct pong { bool pong{}; std::string app{}; };
        const auto res = run(ioc, rt, fut::method::GET, "/api/admin/ping");
        CHECK(res.status == 200);
        const auto v = fut::parse<pong>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->pong == true);
        CHECK(v->app == "admin");
    }

    SUBCASE("admin /echo -- body roundtrip") {
        struct echo_out { std::string message{}; int doubled{}; long long sum{}; };
        const auto res =
            run(ioc, rt, fut::method::POST, "/api/admin/echo", R"({"message":"hi","count":4})");
        CHECK(res.status == 200);
        const auto v = fut::parse<echo_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->message == "hi");
        CHECK(v->doubled == 8);
        CHECK(v->sum == 10);
    }

    SUBCASE("admin /formecho -- the same DTO from urlencoded fields") {
        struct echo_out { std::string message{}; int doubled{}; long long sum{}; };
        const std::pair<std::string_view, std::string_view> headers[] = {
            {"Content-Type", "application/x-www-form-urlencoded"}};
        const auto res = run(ioc, rt, fut::method::POST, "/api/admin/formecho",
                             "message=hi&count=4", {}, headers);
        CHECK(res.status == 200);
        const auto v = fut::parse<echo_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->message == "hi");
        CHECK(v->doubled == 8);
        CHECK(v->sum == 10);
    }

    SUBCASE("admin /upload -- multipart file upload") {
        struct upload_out {
            std::string title{};
            std::string filename{};
            std::size_t size{};
            std::string content_type{};
        };
        std::string body;
        const std::string d = "--fut\r\n";
        body += d;
        body += "Content-Disposition: form-data; name=\"title\"\r\n\r\n";
        body += "quarterly\r\n";
        body += d;
        body += "Content-Disposition: form-data; name=\"file\"; filename=\"q.txt\"\r\n";
        body += "Content-Type: text/plain\r\n\r\n";
        body += "hello from a DLL route\r\n";
        body += "--fut--\r\n";

        const std::pair<std::string_view, std::string_view> headers[] = {
            {"Content-Type", "multipart/form-data; boundary=fut"}};

        const auto res = run(ioc, rt, fut::method::POST, "/api/admin/upload", body, {}, headers);
        CHECK(res.status == 200);
        const auto v = fut::parse<upload_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->title == "quarterly");
        CHECK(v->filename == "q.txt");
        CHECK(v->size == std::string_view("hello from a DLL route").size());
        CHECK(v->content_type == "text/plain");
    }

    SUBCASE("admin /echo -- invalid JSON -> 400") {
        CHECK(run(ioc, rt, fut::method::POST, "/api/admin/echo", "not-json").status == 400);
    }

    SUBCASE("admin /meta -- C++26 std::meta member enumeration") {
        struct member_row { std::string name{}; std::string value_kind{}; };
        struct meta_dto { std::vector<member_row> members{}; std::vector<std::string> names{}; };
        const auto res = run(ioc, rt, fut::method::GET, "/api/admin/meta");
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
        const auto res = run(ioc, rt, fut::method::GET, "/api/admin/whoami");
        CHECK(res.status == 200);
        const auto v = fut::parse<who>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->app == "admin");
        CHECK(v->user == "root");
    }

    SUBCASE("admin /raw -- string action served as text/plain") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/admin/raw");
        CHECK(res.status == 200);
        CHECK(res.content_type.rfind("text/plain", 0) == 0);
        CHECK(res.body.find("fut admin app") == 0);
    }

    SUBCASE("admin /users/hits -- controller instance state") {
        struct hits_out { std::uint64_t hits{}; };
        const auto r1 = run(ioc, rt, fut::method::GET, "/api/admin/users/hits");
        CHECK(r1.status == 200);
        const auto v1 = fut::parse<hits_out>(r1.body);
        REQUIRE(v1.has_value());
        const auto r2 = run(ioc, rt, fut::method::GET, "/api/admin/users/hits");
        const auto v2 = fut::parse<hits_out>(r2.body);
        REQUIRE(v2.has_value());
        // one instance per DLL: the counter advances between calls
        CHECK(v2->hits == v1->hits + 1);
    }

    SUBCASE("admin /users/7/name -- controller path + placeholder") {
        struct name_out { long long id{}; std::string name{}; };
        const auto res = run(ioc, rt, fut::method::GET, "/api/admin/users/7/name");
        CHECK(res.status == 200);
        const auto v = fut::parse<name_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->id == 7);
        CHECK(v->name == "user-7");
    }

    SUBCASE("web /hello?name=fut -- query binding") {
        struct hello { std::string hello{}; };
        const auto res = run(ioc, rt, fut::method::GET, "/api/web/hello", "", "name=fut");
        CHECK(res.status == 200);
        const auto v = fut::parse<hello>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->hello == "hello, fut");
    }

    SUBCASE("web /calc -- body DTO binding") {
        struct calc_out { double result{}; std::string op{}; };
        const auto res =
            run(ioc, rt, fut::method::POST, "/api/web/calc", R"({"a":6,"b":7,"op":"mul"})");
        CHECK(res.status == 200);
        const auto v = fut::parse<calc_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->result == doctest::Approx(42.0));
        CHECK(v->op == "mul");
    }

    SUBCASE("web /calc -- division by zero -> 400") {
        CHECK(run(ioc, rt, fut::method::POST, "/api/web/calc", R"({"a":6,"b":0,"op":"div"})").status ==
              400);
    }

    SUBCASE("web /sum/6/7 -- path placeholder binding") {
        struct sum_out { long long a{}; long long b{}; long long sum{}; };
        const auto res = run(ioc, rt, fut::method::GET, "/api/web/sum/6/7");
        CHECK(res.status == 200);
        const auto v = fut::parse<sum_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->sum == 13);
    }

    SUBCASE("web /note -- string action") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/web/note");
        CHECK(res.status == 200);
        CHECK(res.content_type.rfind("text/plain", 0) == 0);
        CHECK(res.body.find("fut web app") == 0);
    }

    SUBCASE("web /beta/note -- controller in a nested namespace") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/web/beta/note");
        CHECK(res.status == 200);
        CHECK(res.content_type.rfind("text/plain", 0) == 0);
        CHECK(res.body == "from namespace web::beta");
    }

    SUBCASE("admin /log -- the injected group writes into the server's chains") {
        captured().clear();
        struct log_dto {
            bool logged{};
            std::string group{};
        };
        const auto res = run(ioc, rt, fut::method::GET, "/api/admin/log");
        CHECK(res.status == 200);
        const auto v = fut::parse<log_dto>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->logged);
        CHECK(v->group == "admin.log");

        REQUIRE(!captured().empty());
        CHECK(contains(captured(), "admin.log", "admin /log was called"));
        CHECK(contains(captured(), "admin.log", "and this one is a warning"));
    }

    SUBCASE("admin /io -- the injected io_context is the live server loop") {
        struct io_dto {
            bool injected{};
            bool timer_fired{};
        };
        const auto res = run(ioc, rt, fut::method::GET, "/api/admin/io");
        CHECK(res.status == 200);
        const auto v = fut::parse<io_dto>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->injected);
        CHECK(v->timer_fired);   // the timer fired on the injected loop
    }

    SUBCASE("admin /fail/418 -- the action picks the status") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/admin/fail/418");
        CHECK(res.status == 418);
        CHECK(res.body.find("requested failure 418") != std::string::npos);
    }

    SUBCASE("platform middleware sees every response, including 404") {
        const auto ok = run(ioc, rt, fut::method::GET, "/api/admin/ping");
        CHECK(has_header(ok, "X-Platform", "1"));

        const auto missing = run(ioc, rt, fut::method::GET, "/api/nope");
        CHECK(missing.status == 404);
        CHECK(has_header(missing, "X-Platform", "1"));
    }

    SUBCASE("platform middleware can short-circuit") {
        const std::pair<std::string_view, std::string_view> headers[] = {{"x-block", "1"}};
        const auto res = run(ioc, rt, fut::method::GET, "/api/admin/ping", {}, {}, headers);
        CHECK(res.status == 401);
    }

    SUBCASE("the admin app's own middleware covers admin routes only") {
        CHECK(has_header(run(ioc, rt, fut::method::GET, "/api/admin/ping"), "X-Fut-App", "admin"));
        CHECK_FALSE(has_header(run(ioc, rt, fut::method::GET, "/api/web/ping"), "X-Fut-App", "admin"));
    }

    SUBCASE("unknown route -> 404") {
        CHECK(run(ioc, rt, fut::method::GET, "/api/admin/nope").status == 404);
        CHECK(run(ioc, rt, fut::method::GET, "/api/web/nope").status == 404);
    }
}

} // TEST_SUITE
