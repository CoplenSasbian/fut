#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio.hpp>

#include <fut/app.hpp>
#include <fut/http/http.hpp>
#include <fut/json.hpp>

namespace asio = boost::asio;

// ---- test DTOs ----

struct ping_dto { bool pong{true}; std::string app{"demo"}; };
struct add_in { int a{}; int b{}; };
struct add_out { int result{}; };
struct greet_out { std::string hi{}; };
struct mul_out { int result{}; };
struct raw_out { std::string method{}; std::string target{}; };
struct who_dto { std::string app{"demo"}; int n{3}; };

// ---- composition by stacking: two independent annotations on one class ----
//
// No inheritance and no base calls. Each annotation is a separate opinion
// about the class; they are applied in declaration order. When one needs
// data another carries, it reads it from the shared entity -- the entity is
// the composition context.

struct health_dto { bool ok{true}; };

asio::awaitable<fut::response> health_route(const fut::request&, const fut::path_match&) {
    co_return fut::json(health_dto{});
}

// Adds <controller path>/health to whatever class it is put on.
struct health : fut::scannable {
    template <std::meta::info Entity>
    void operation(fut::fut_ctx* ctx) const {
        constexpr auto controller_ann = fut::ann::first<fut::controller>(Entity);
        if constexpr (controller_ann != std::meta::info{}) {
            constexpr auto controller_value = [: std::meta::constant_of(controller_ann) :];
            const fut::http::router_module* rm = fut::http::router_of(ctx);
            if (!rm || !rm->add_route) return;
            rm->add_route(rm->self, fut::method::GET,
                          fut::join_path(ctx->app_prefix, controller_value.path.view(), "/health"),
                          &health_route);
        }
    }
};

// ---- controllers covering every binding style ----

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

    // plain string return -> text/plain
    [[=fut::http_get("/txt")]]
    asio::awaitable<std::string> txt() { co_return "plain text from fut"; }

    [[=fut::http_get("/mix/{n}")]]
    asio::awaitable<std::string> mixed([[=fut::from_path("n")]] int n) {
        co_return "n squared is " + std::to_string(n * n);
    }

    // A controller nested inside a class: the scan never descends into a
    // class, so this one must NOT be registered (see the test below).
    struct [[=fut::controller("/inner")]] inner_controller {
        [[=fut::http_get("/hidden")]]
        asio::awaitable<greet_out> hidden() { co_return greet_out{"unreachable"}; }
    };
};

struct [[=fut::controller("/grp")]] grouped_controller {
    [[=fut::http_get("/first")]]
    asio::awaitable<greet_out> first() { co_return greet_out{"grouped"}; }
};

// An entity-agnostic annotation: plain operation(ctx), no template parameter.
// Useful for app-wide setup that does not depend on which class carries it.
struct always_health : fut::scannable {
    void operation(fut::fut_ctx* ctx) const {
        const fut::http::router_module* rm = fut::http::router_of(ctx);
        if (!rm || !rm->add_route) return;
        rm->add_route(rm->self, fut::method::GET,
                      fut::join_path(ctx->app_prefix, "/health"), &health_route);
    }
};

struct [[=always_health{}]] app_marker {};

struct [[=fut::controller("/mon")]] [[=health{}]] monitored_controller {
    [[=fut::http_get("/ping")]]
    asio::awaitable<greet_out> ping() { co_return greet_out{"monitored"}; }
};

namespace nested {
struct [[=fut::controller("/deep")]] nested_controller {
    [[=fut::http_get("/one")]]
    asio::awaitable<greet_out> deep_one() { co_return greet_out{"deep"}; }
};
} // namespace nested

} // namespace demo_actions

namespace {

// Collects what a registering annotation hands to the router module.
struct sink {
    struct route {
        fut::method method;
        std::string pattern;
        fut::http::route_fn invoke;
    };

    std::vector<route> routes{};
    fut::http::router_module module{this, &add_thunk};

    static void add_thunk(void* self, fut::method m, std::string_view pattern,
                          fut::http::route_fn fn) {
        static_cast<sink*>(self)->routes.push_back(route{m, std::string(pattern), fn});
    }
};

// A minimal stand-in for the server: context + "router" module + one scan.
struct harness {
    sink s{};
    fut::fut_ctx ctx{};

    explicit harness(std::string_view prefix) : prefix_storage(prefix) {
        ctx.host = &s;
        ctx.app_name = "demo";
        ctx.app_prefix = prefix_storage;
        ctx.get_module_fn = [](fut::fut_ctx* c, std::string_view name) -> void* {
            if (name == "router") return &static_cast<sink*>(c->host)->module;
            return nullptr;
        };
        fut::scan::run<^^demo_actions>(&ctx);
    }

    std::string prefix_storage;

    [[nodiscard]] const sink::route* find(fut::method m, std::string_view pattern) const {
        for (const auto& r : s.routes)
            if (r.method == m && r.pattern == pattern) return &r;
        return nullptr;
    }
};

struct run_state {
    fut::response res{};
    std::exception_ptr err{};
};

asio::awaitable<void> invoke_into(run_state& st, fut::http::route_fn fn, fut::request req,
                                  fut::path_match pm) {
    try {
        st.res = co_await fn(req, pm);
    } catch (...) {
        st.err = std::current_exception();
    }
}

// Dispatches like the server: find a matching route, run it, map bind errors.
fut::response drive(sink& s, fut::method m, std::string_view path,
                    std::string_view body = {}, std::string_view query = {}) {
    for (const auto& r : s.routes) {
        if (r.method != m) continue;
        fut::path_match pm;
        if (!fut::match_pattern(r.pattern, path, pm)) continue;

        asio::io_context ioc;
        run_state st;
        asio::co_spawn(ioc,
                       invoke_into(st, r.invoke,
                                   fut::request{fut::to_string(m), path, path, query, body, {}},
                                   pm),
                       [](std::exception_ptr) {});
        ioc.run();
        if (st.err) {
            try {
                std::rethrow_exception(st.err);
            } catch (const fut::bind_error& e) {
                return fut::error(400, e.what());
            }
        }
        return st.res;
    }
    return fut::error(404, "no route");
}

} // namespace

TEST_SUITE("scanned controller registration") {

TEST_CASE("the scan registers every verb-annotated member, prefix composed") {
    harness h{"/api/demo"};
    REQUIRE(h.s.routes.size() == 13);  // 8 main + 1 grouped + 1 nested + 2 monitored + 1 app-wide

    CHECK(h.find(fut::method::GET, "/api/demo/ping") != nullptr);
    CHECK(h.find(fut::method::POST, "/api/demo/add") != nullptr);
    CHECK(h.find(fut::method::GET, "/api/demo/mul/{x}/{y}") != nullptr);
    CHECK(h.find(fut::method::GET, "/api/demo/grp/first") != nullptr);   // controller path
    CHECK(h.find(fut::method::GET, "/api/demo/deep/one") != nullptr);    // nested namespace

    // stacked peer: the controller's route plus the health peer's own route
    CHECK(h.find(fut::method::GET, "/api/demo/mon/ping") != nullptr);
    CHECK(h.find(fut::method::GET, "/api/demo/mon/health") != nullptr);

    // the entity-agnostic annotation: plain operation(ctx), no template
    CHECK(h.find(fut::method::GET, "/api/demo/health") != nullptr);
}

TEST_CASE("an entity-agnostic annotation is dispatched too") {
    harness h{"/api/demo"};
    const auto res = drive(h.s, fut::method::GET, "/api/demo/health");
    CHECK(res.status == 200);
    const auto v = fut::parse<health_dto>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->ok == true);
}

TEST_CASE("stacked annotations compose through the shared entity") {
    harness h{"/api/demo"};

    // the controller annotation owns the member route
    const auto ping = drive(h.s, fut::method::GET, "/api/demo/mon/ping");
    CHECK(ping.status == 200);
    const auto v = fut::parse<greet_out>(ping.body);
    REQUIRE(v.has_value());
    CHECK(v->hi == "monitored");

    // the peer annotation added its own route, using the controller's path
    // that it read from the same entity
    const auto res = drive(h.s, fut::method::GET, "/api/demo/mon/health");
    CHECK(res.status == 200);
    const auto ok = fut::parse<health_dto>(res.body);
    REQUIRE(ok.has_value());
    CHECK(ok->ok == true);
}

TEST_CASE("the scan does not recurse into classes") {
    harness h{"/api/demo"};
    // 10 routes: the nested controller inside main_controller was skipped,
    // because only namespaces are recursed into.
    CHECK(h.s.routes.size() == 13);
    CHECK(h.find(fut::method::GET, "/api/demo/inner/hidden") == nullptr);
    CHECK(drive(h.s, fut::method::GET, "/api/demo/inner/hidden").status == 404);
}

TEST_CASE("an empty app prefix still yields absolute paths") {
    harness h{""};
    CHECK(h.find(fut::method::GET, "/ping") != nullptr);
    CHECK(h.find(fut::method::GET, "/grp/first") != nullptr);
}

TEST_CASE("GET /ping") {
    harness h{"/api/demo"};
    const auto res = drive(h.s, fut::method::GET, "/api/demo/ping");
    CHECK(res.status == 200);
    const auto v = fut::parse<ping_dto>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->pong == true);
    CHECK(v->app == "demo");
}

TEST_CASE("POST /add") {
    harness h{"/api/demo"};
    const auto res = drive(h.s, fut::method::POST, "/api/demo/add", R"({"a":2,"b":5})");
    CHECK(res.status == 200);
    const auto v = fut::parse<add_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 7);
}

TEST_CASE("POST /add -- invalid JSON -> 400") {
    harness h{"/api/demo"};
    CHECK(drive(h.s, fut::method::POST, "/api/demo/add", "not-json").status == 400);
}

TEST_CASE("GET /greet -- query binding, empty key uses the parameter name") {
    harness h{"/api/demo"};
    const auto res = drive(h.s, fut::method::GET, "/api/demo/greet", "", "name=fut");
    CHECK(res.status == 200);
    const auto v = fut::parse<greet_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->hi == "hi fut");
}

TEST_CASE("GET /greet -- missing query -> 400") {
    harness h{"/api/demo"};
    CHECK(drive(h.s, fut::method::GET, "/api/demo/greet").status == 400);
}

TEST_CASE("GET /mul/6/7 -- path placeholder binding") {
    harness h{"/api/demo"};
    const auto res = drive(h.s, fut::method::GET, "/api/demo/mul/6/7");
    CHECK(res.status == 200);
    const auto v = fut::parse<mul_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 42);
}

TEST_CASE("GET /mul/6/xx -- non-numeric -> 400") {
    harness h{"/api/demo"};
    CHECK(drive(h.s, fut::method::GET, "/api/demo/mul/6/xx").status == 400);
}

TEST_CASE("GET /raw -- const request& escape hatch") {
    harness h{"/api/demo"};
    const auto res = drive(h.s, fut::method::GET, "/api/demo/raw");
    CHECK(res.status == 200);
    const auto v = fut::parse<raw_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->method == "GET");
    CHECK(v->target == "/api/demo/raw");
}

TEST_CASE("GET /txt -- plain string return") {
    harness h{"/api/demo"};
    const auto res = drive(h.s, fut::method::GET, "/api/demo/txt");
    CHECK(res.status == 200);
    CHECK(res.content_type.rfind("text/plain", 0) == 0);
    CHECK(res.body == "plain text from fut");
}

TEST_CASE("GET /mix/9 -- string + path binding") {
    harness h{"/api/demo"};
    const auto res = drive(h.s, fut::method::GET, "/api/demo/mix/9");
    CHECK(res.status == 200);
    CHECK(res.body == "n squared is 81");
}

TEST_CASE("no match -> 404") {
    harness h{"/api/demo"};
    CHECK(drive(h.s, fut::method::GET, "/api/demo/nope").status == 404);
    CHECK(drive(h.s, fut::method::POST, "/api/demo/ping").status == 404);
}

} // TEST_SUITE
