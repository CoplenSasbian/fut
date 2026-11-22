#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <exception>
#include <span>
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
struct upload_out {
    std::string filename{};
    std::size_t size{};
    std::string content_type{};
};

// ---- composition by stacking: two independent annotations on one class ----
//
// No inheritance and no base calls. Each annotation is a separate opinion
// about the class; they are applied in declaration order. When one needs
// data another carries, it reads it from the shared entity -- the entity is
// the composition context.

struct health_dto { bool ok{true}; };

asio::awaitable<void> health_route(const fut::request&, const fut::path_match&,
                                   fut::response& out) {
    out.dto(health_dto{});
    co_return;
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

struct store {
    int id{};
};

// injected while the namespace is scanned: the pointee type is the key
[[=fut::inject{}]] boost::asio::io_context* g_io = nullptr;

// two modules of one type: the name decides, and with no name the variable's
// own identifier is tried before giving up
[[=fut::inject{"primary"}]] store* primary_store = nullptr;
[[=fut::inject{}]] store* archive = nullptr;

// a group bound to whatever service the context publishes
[[=fut::log_group{}]] fut::log::group_ref app_log;
[[=fut::log_group{"special"}]] fut::log::group_ref named_log;

struct [[=fut::controller{}]] main_controller {
    [[=fut::http_get("/ping")]]
    asio::awaitable<void> ping(fut::response& out) { out.dto(ping_dto{}); co_return; }

    [[=fut::http_post("/add")]]
    asio::awaitable<void> add([[=fut::from_body{}]] add_in in, fut::response& out) {
        out.dto(add_out{in.a + in.b});
        co_return;
    }

    // empty key -> parameter name ("name")
    [[=fut::http_get("/greet")]]
    asio::awaitable<void> greet([[=fut::from_query{}]] std::string name,
                                fut::response& out) {
        out.dto(greet_out{"hi " + name});
        co_return;
    }

    [[=fut::http_get("/mul/{x}/{y}")]]
    asio::awaitable<void> mul([[=fut::from_path("x")]] int x,
                              [[=fut::from_path("y")]] int y, fut::response& out) {
        out.dto(mul_out{x * y});
        co_return;
    }

    // escape hatch: no annotation, const fut::request&
    [[=fut::http_get("/raw")]]
    asio::awaitable<void> raw(const fut::request& req, fut::response& out) {
        out.dto(raw_out{std::string(req.method), std::string(req.target)});
        co_return;
    }

    [[=fut::http_get("/who")]]
    asio::awaitable<void> who(fut::response& out) { out.dto(who_dto{}); co_return; }

    // plain string return -> text/plain
    [[=fut::http_get("/txt")]]
    asio::awaitable<void> txt(fut::response& out) {
        out.set_text("plain text from fut");
        co_return;
    }

    [[=fut::http_get("/mix/{n}")]]
    asio::awaitable<void> mixed([[=fut::from_path("n")]] int n, fut::response& out) {
        out.set_text("n squared is " + std::to_string(n * n));
        co_return;
    }

    // form fields: urlencoded or multipart, empty key -> parameter name
    [[=fut::http_post("/form")]]
    asio::awaitable<void> form([[=fut::from_form{}]] std::string name,
                               [[=fut::from_form("n")]] int n, fut::response& out) {
        out.dto(greet_out{name + "/" + std::to_string(n)});
        co_return;
    }

    // a whole DTO from a form: members are matched by name
    [[=fut::http_post("/formdto")]]
    asio::awaitable<void> form_dto([[=fut::from_form{}]] add_in in, fut::response& out) {
        out.dto(add_out{in.a + in.b});
        co_return;
    }

    // a whole DTO from query parameters
    [[=fut::http_get("/querydto")]]
    asio::awaitable<void> query_dto([[=fut::from_query{}]] add_in in, fut::response& out) {
        out.dto(add_out{in.a + in.b});
        co_return;
    }

    // an uploaded file from a multipart part
    [[=fut::http_post("/upload")]]
    asio::awaitable<void> upload([[=fut::from_file{}]] fut::uploaded_file file,
                                 fut::response& out) {
        out.dto(upload_out{std::string(file.filename), file.size(),
                           std::string(file.content_type)});
        co_return;
    }

    // A controller nested inside a class: the scan never descends into a
    // class, so this one must NOT be registered (see the test below).
    struct [[=fut::controller("/inner")]] inner_controller {
        [[=fut::http_get("/hidden")]]
        asio::awaitable<void> hidden(fut::response& out) {
            out.dto(greet_out{"unreachable"});
            co_return;
        }
    };
};

struct [[=fut::controller("/grp")]] grouped_controller {
    [[=fut::http_get("/first")]]
    asio::awaitable<void> first(fut::response& out) { out.dto(greet_out{"grouped"}); co_return; }
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
    asio::awaitable<void> ping(fut::response& out) {
        out.dto(greet_out{"monitored"});
        co_return;
    }
};

namespace nested {
struct [[=fut::controller("/deep")]] nested_controller {
    [[=fut::http_get("/one")]]
    asio::awaitable<void> deep_one(fut::response& out) {
        out.dto(greet_out{"deep"});
        co_return;
    }
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
    asio::io_context ioc{};   // what the "io" module points at
    fut::log::service log_service{};   // the "log" module
    demo_actions::store primary_store{1};
    demo_actions::store archive_store{2};

    explicit harness(std::string_view prefix) : prefix_storage(prefix) {
        ctx.host = this;
        ctx.app_name = "demo";
        ctx.app_prefix = prefix_storage;
        ctx.get_module_fn = [](fut::fut_ctx* c, std::string_view name,
                               std::string_view type) -> fut::module_lookup {
            harness* self = static_cast<harness*>(c->host);
            const bool router_type = type == typeid(fut::http::router_module).name();
            const bool io_type = type == typeid(asio::io_context).name();
            const bool log_type = type == typeid(fut::log::service).name();
            const bool store_type = type == typeid(demo_actions::store).name();
            if (!type.empty() && !router_type && !io_type && !log_type && !store_type)
                return {nullptr, fut::module_status::wrong_type};

            if (log_type || name == "log")
                return {&self->log_service, fut::module_status::found};

            if (store_type) {
                if (name.empty()) return {nullptr, fut::module_status::ambiguous};
                if (name == "primary") return {&self->primary_store, fut::module_status::found};
                if (name == "archive") return {&self->archive_store, fut::module_status::found};
                return {nullptr, fut::module_status::not_found};
            }

            if (io_type || name == "io")
                return {&self->ioc, fut::module_status::found};

            const bool name_ok = name.empty() || name == "router";
            if (!name_ok) return {nullptr, fut::module_status::not_found};
            return {&self->s.module, fut::module_status::found};
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

// A browser-style multipart body for the upload tests.
std::string multipart_body() {
    std::string b;
    const std::string d = "--fut\r\n";
    b += d;
    b += "Content-Disposition: form-data; name=\"name\"\r\n\r\n";
    b += "fut\r\n";
    b += d;
    b += "Content-Disposition: form-data; name=\"n\"\r\n\r\n";
    b += "7\r\n";
    b += d;
    b += "Content-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n";
    b += "Content-Type: text/plain\r\n\r\n";
    b += "hello upload\r\n";
    b += "--fut--\r\n";
    return b;
}

struct run_state {
    fut::response res{};
    std::exception_ptr err{};
};

asio::awaitable<void> invoke_into(run_state& st, fut::http::route_fn fn, fut::request req,
                                  fut::path_match pm) {
    try {
        co_await fn(req, pm, st.res);   // the action writes the answer in place
    } catch (const fut::bind_error& e) {
        st.res = fut::error(400, e.what());
    } catch (...) {
        st.err = std::current_exception();
    }
}

// Dispatches like the server: find a matching route, run it, map bind errors.
fut::response drive(sink& s, fut::method m, std::string_view path,
                    std::string_view body = {}, std::string_view query = {},
                    std::span<const std::pair<std::string_view, std::string_view>> headers = {}) {
    for (const auto& r : s.routes) {
        if (r.method != m) continue;
        fut::path_match pm;
        if (!fut::match_pattern(r.pattern, path, pm)) continue;

        asio::io_context ioc;
        run_state st;
        asio::co_spawn(ioc,
                       invoke_into(st, r.invoke,
                                   fut::request{fut::to_string(m), path, path, query, body,
                                                headers},
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
    REQUIRE(h.s.routes.size() == 17);  // 12 main + 1 grouped + 1 nested + 2 monitored + 1 app-wide

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

    // form bindings
    CHECK(h.find(fut::method::POST, "/api/demo/form") != nullptr);
    CHECK(h.find(fut::method::POST, "/api/demo/upload") != nullptr);
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

TEST_CASE("a namespace variable is injected from the context") {
    harness h{"/api/demo"};
    // the scanner assigned the pointer while it walked the namespace
    REQUIRE(demo_actions::g_io != nullptr);
    CHECK(demo_actions::g_io == &h.ioc);
}

TEST_CASE("fut::log_group builds an app-scoped group_ref") {
    harness h{"/api/demo"};
    CHECK(demo_actions::app_log.name() == "demo.app_log");   // app name + identifier
    CHECK(demo_actions::app_log.sink() == &h.log_service);   // wired to the service

    // an explicit name wins over the derived one
    CHECK(demo_actions::named_log.name() == "special");
    CHECK(demo_actions::named_log.sink() == &h.log_service);
}

TEST_CASE("fut::inject picks among modules of one type by name") {
    harness h{"/api/demo"};

    // the annotation's name
    REQUIRE(demo_actions::primary_store != nullptr);
    CHECK(demo_actions::primary_store->id == 1);

    // no name given: the variable's own identifier ("archive")
    REQUIRE(demo_actions::archive != nullptr);
    CHECK(demo_actions::archive->id == 2);
}

TEST_CASE("the scan does not recurse into classes") {
    harness h{"/api/demo"};
    // 10 routes: the nested controller inside main_controller was skipped,
    // because only namespaces are recursed into.
    CHECK(h.s.routes.size() == 17);
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

TEST_CASE("POST /form -- urlencoded fields") {
    harness h{"/api/demo"};
    const std::pair<std::string_view, std::string_view> headers[] = {
        {"Content-Type", "application/x-www-form-urlencoded"}};
    const auto res = drive(h.s, fut::method::POST, "/api/demo/form", "name=fut&n=7", {},
                           headers);
    CHECK(res.status == 200);
    const auto v = fut::parse<greet_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->hi == "fut/7");
}

TEST_CASE("POST /formdto -- a whole DTO from form fields") {
    harness h{"/api/demo"};
    const std::pair<std::string_view, std::string_view> headers[] = {
        {"Content-Type", "application/x-www-form-urlencoded"}};
    const auto res = drive(h.s, fut::method::POST, "/api/demo/formdto", "a=2&b=5", {},
                           headers);
    CHECK(res.status == 200);
    const auto v = fut::parse<add_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 7);
}

TEST_CASE("POST /formdto -- absent fields keep their defaults") {
    harness h{"/api/demo"};
    const std::pair<std::string_view, std::string_view> headers[] = {
        {"Content-Type", "application/x-www-form-urlencoded"}};
    const auto res = drive(h.s, fut::method::POST, "/api/demo/formdto", "a=4", {}, headers);
    CHECK(res.status == 200);
    const auto v = fut::parse<add_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 4);   // add_in::b defaults to 0
}

TEST_CASE("GET /querydto -- a whole DTO from query parameters") {
    harness h{"/api/demo"};
    const auto res = drive(h.s, fut::method::GET, "/api/demo/querydto", "", "a=6&b=7");
    CHECK(res.status == 200);
    const auto v = fut::parse<add_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->result == 13);
}

TEST_CASE("POST /form -- multipart fields") {
    harness h{"/api/demo"};
    const std::string body = multipart_body();
    const std::pair<std::string_view, std::string_view> headers[] = {
        {"Content-Type", "multipart/form-data; boundary=fut"}};
    const auto res = drive(h.s, fut::method::POST, "/api/demo/form", body, {}, headers);
    CHECK(res.status == 200);
    const auto v = fut::parse<greet_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->hi == "fut/7");
}

TEST_CASE("POST /upload -- the file arrives intact") {
    harness h{"/api/demo"};
    const std::string body = multipart_body();
    const std::pair<std::string_view, std::string_view> headers[] = {
        {"Content-Type", "multipart/form-data; boundary=fut"}};
    const auto res = drive(h.s, fut::method::POST, "/api/demo/upload", body, {}, headers);
    CHECK(res.status == 200);
    const auto v = fut::parse<upload_out>(res.body);
    REQUIRE(v.has_value());
    CHECK(v->filename == "a.txt");
    CHECK(v->size == std::string_view("hello upload").size());
    CHECK(v->content_type == "text/plain");
}

TEST_CASE("form bindings reject a non-form body") {
    harness h{"/api/demo"};
    const std::pair<std::string_view, std::string_view> headers[] = {
        {"Content-Type", "application/json"}};
    CHECK(drive(h.s, fut::method::POST, "/api/demo/form", R"({"name":"x"})", {}, headers)
              .status == 400);
}

TEST_CASE("no match -> 404") {
    harness h{"/api/demo"};
    CHECK(drive(h.s, fut::method::GET, "/api/demo/nope").status == 404);
    CHECK(drive(h.s, fut::method::POST, "/api/demo/ping").status == 404);
}

} // TEST_SUITE
