#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <boost/asio.hpp>

#include <fut/http/error.hpp>
#include <fut/http/http.hpp>
#include <fut/json.hpp>
#include <fut/log.hpp>

#include <fut/exec/app_loader.hpp>
#include <fut/exec/module_registry.hpp>
#include <fut/exec/router.hpp>

namespace asio = boost::asio;

namespace {

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

// Dispatches exactly like the server: the router runs the middleware chains,
// matches the route, maps errors and returns a response.
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

bool has_header(const fut::response& res, std::string_view name, std::string_view value) {
    for (const auto& [k, v] : res.headers)
        if (k == name && v == value) return true;
    return false;
}

bool has_header(const fut::response& res, std::string_view name) {
    for (const auto& [k, v] : res.headers)
        if (k == name) return true;
    return false;
}

const fut::exec::app_loader::entry* find_app(
    const std::vector<fut::exec::app_loader::entry>& apps, std::string_view name) {
    for (const auto& a : apps)
        if (a.name == name) return &a;
    return nullptr;
}

// Captures what the app logs, so the test can prove the injected group really
// writes into the server's chains.
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

bool contains(const std::vector<logged_line>& lines, std::string_view group,
              std::string_view message) {
    for (const auto& line : lines)
        if (line.group == group && line.message == message) return true;
    return false;
}

} // namespace

TEST_SUITE("demo app DLL end-to-end") {

TEST_CASE("load fut_app_demo and drive its routes") {
    const char* dir = std::getenv("FUT_APPS_DIR");
    REQUIRE(dir != nullptr);

    // Declared first, destroyed last: the DLLs stay loaded until the router
    // and the io_context (which may hold DLL handlers) are gone.
    fut::exec::app_loader loader;
    asio::io_context ioc;
    fut::exec::router rt;
    fut::exec::module_registry modules;
    modules.add("io", typeid(asio::io_context).name(), &ioc);
    static const fut::log::service log_service{nullptr, &capture};
    modules.add("log", typeid(fut::log::service).name(),
                const_cast<fut::log::service*>(&log_service));

    const auto apps = loader.load_dir(dir, rt, modules);
    REQUIRE(apps.size() == 1);

    // Platform middleware: the executable's, sees every request.
    rt.add_global_middleware([](const fut::request& req, const fut::path_match& params,
                                const fut::http::next_fn& next)
                                 -> asio::awaitable<fut::response> {
        if (req.header("x-block") == "1") co_return fut::error(401, "blocked");
        fut::response res = co_await next(req, params);
        res.headers.emplace_back("X-Platform", "1");
        co_return res;
    });
    rt.finalize();

    const auto* demo_app = find_app(apps, "demo");
    REQUIRE(demo_app != nullptr);
    CHECK(demo_app->prefix == "/api/demo");
    CHECK(demo_app->routes == 27);   // 20 main + 4 items + 1 nested + 3 binding-free

    SUBCASE("GET /ping -- a DTO becomes JSON") {
        struct pong {
            bool pong{};
            std::string app{};
        };
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/ping");
        CHECK(res.status == 200);
        const auto v = fut::parse<pong>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->pong);
        CHECK(v->app == "demo");
    }

    SUBCASE("GET /info -- routes are reflected, not hand-listed") {
        struct route_entry {
            std::string method{};
            std::string path{};
        };
        struct info {
            std::string name{};
            std::string prefix{};
            unsigned abi{};
            std::vector<route_entry> routes{};
        };
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/info");
        CHECK(res.status == 200);
        const auto v = fut::parse<info>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->name == "demo");
        CHECK(v->prefix == "/api/demo");
        CHECK(v->abi == fut::abi_version);
        CHECK(v->routes.size() == 27);
        bool found_put = false;
        for (const auto& r : v->routes)
            if (r.method == "PUT" && r.path == "/api/demo/items/{id}") found_put = true;
        CHECK(found_put);
    }

    SUBCASE("GET /text -- a std::string is text/plain") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/text");
        CHECK(res.status == 200);
        CHECK(res.body.find("fut demo app") != std::string::npos);
    }

    SUBCASE("GET /custom -- fut::response gives full control") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/custom");
        CHECK(res.status == 201);
        CHECK(has_header(res, "X-Demo-Custom", "1"));
        CHECK(has_header(res, "Location", "/api/demo/ping"));
        CHECK(res.body == R"({"created":true})");
    }

    SUBCASE("GET /query -- one parameter at a time") {
        struct greeting {
            std::string message{};
            int doubled{};
            long long sum{};
        };
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/query", "", "name=yue&count=4");
        CHECK(res.status == 200);
        const auto v = fut::parse<greeting>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->message == "yue!");
        CHECK(v->doubled == 8);
        CHECK(v->sum == 10);
    }

    SUBCASE("GET /query -- a missing parameter is a 400") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/query", "", "name=yue");
        CHECK(res.status == 400);
        CHECK(res.body.find("count") != std::string::npos);
    }

    SUBCASE("GET /query/dto -- the whole DTO, defaults kept") {
        struct query_in {
            std::string name{};
            int count{1};
            bool verbose{false};
        };
        const auto empty = fut::parse<query_in>(
            run(ioc, rt, fut::method::GET, "/api/demo/query/dto", "", "name=only").body);
        REQUIRE(empty.has_value());
        CHECK(empty->name == "only");
        CHECK(empty->count == 1);
        CHECK(empty->verbose == false);

        const auto full = fut::parse<query_in>(
            run(ioc, rt, fut::method::GET, "/api/demo/query/dto", "", "name=x&count=3&verbose=1")
                .body);
        REQUIRE(full.has_value());
        CHECK(full->count == 3);
        CHECK(full->verbose == true);
    }

    SUBCASE("GET /header -- from_header, and a missing header is a 400") {
        const std::pair<std::string_view, std::string_view> headers[] = {
            {"X-Token", "secret-token"}};
        struct pong {
            bool pong{};
            std::string app{};
        };
        const auto ok = run(ioc, rt, fut::method::GET, "/api/demo/header", "", "", headers);
        CHECK(ok.status == 200);
        const auto v = fut::parse<pong>(ok.body);
        REQUIRE(v.has_value());
        CHECK(v->pong);
        CHECK(v->app == "secret-token");

        CHECK(run(ioc, rt, fut::method::GET, "/api/demo/header").status == 400);
    }

    SUBCASE("POST /body -- the whole DTO from JSON") {
        struct greeting {
            std::string message{};
            int doubled{};
            long long sum{};
        };
        const auto res = run(ioc, rt, fut::method::POST, "/api/demo/body",
                             R"({"message":"hi","count":4})");
        CHECK(res.status == 200);
        const auto v = fut::parse<greeting>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->message == "hi");
        CHECK(v->doubled == 8);
        CHECK(v->sum == 10);
    }

    SUBCASE("POST /body -- malformed JSON is a 400") {
        const auto res = run(ioc, rt, fut::method::POST, "/api/demo/body", "not json");
        CHECK(res.status == 400);
        CHECK(res.body.find("invalid JSON body") != std::string::npos);
    }

    SUBCASE("POST /body/scalar -- a scalar from the body") {
        struct greeting {
            std::string message{};
            int doubled{};
            long long sum{};
        };
        const auto res = run(ioc, rt, fut::method::POST, "/api/demo/body/scalar", "21");
        CHECK(res.status == 200);
        const auto v = fut::parse<greeting>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->doubled == 42);
    }

    SUBCASE("POST /form -- the same DTO from urlencoded fields") {
        struct greeting {
            std::string message{};
            int doubled{};
            long long sum{};
        };
        const std::pair<std::string_view, std::string_view> headers[] = {
            {"Content-Type", "application/x-www-form-urlencoded"}};
        const auto res = run(ioc, rt, fut::method::POST, "/api/demo/form",
                             "message=formy&count=3", "", headers);
        CHECK(res.status == 200);
        const auto v = fut::parse<greeting>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->message == "formy");
        CHECK(v->doubled == 6);
        CHECK(v->sum == 6);
    }

    SUBCASE("POST /upload -- a text field and a file from multipart") {
        struct upload {
            std::string title{};
            std::string filename{};
            std::size_t bytes{};
            std::string content_type{};
        };
        const std::string boundary = "----futdemo";
        const std::string body = "--" + boundary +
            "\r\nContent-Disposition: form-data; name=\"title\"\r\n\r\nhello\r\n"
            "--" + boundary +
            "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n"
            "Content-Type: text/plain\r\n\r\nupload-payload\r\n"
            "--" + boundary + "--\r\n";
        const std::pair<std::string_view, std::string_view> headers[] = {
            {"Content-Type", "multipart/form-data; boundary=" + boundary}};
        const auto res = run(ioc, rt, fut::method::POST, "/api/demo/upload", body, "", headers);
        CHECK(res.status == 200);
        const auto v = fut::parse<upload>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->title == "hello");
        CHECK(v->filename == "a.txt");
        CHECK(v->bytes == std::string_view{"upload-payload"}.size());
        CHECK(v->content_type == "text/plain");
    }

    SUBCASE("PUT /items/{id} -- path parameter and body together") {
        struct item {
            long long id{};
            std::string name{};
            int quantity{};
        };
        const auto res = run(ioc, rt, fut::method::PUT, "/api/demo/items/7",
                             R"({"name":"widget","quantity":3})");
        CHECK(res.status == 200);
        const auto v = fut::parse<item>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->id == 7);          // from the path, not the body
        CHECK(v->name == "widget");
        CHECK(v->quantity == 3);
    }

    SUBCASE("DELETE /items/{id} -- a 204 with no body") {
        const auto res = run(ioc, rt, fut::method::DEL, "/api/demo/items/9");
        CHECK(res.status == 204);
        CHECK(has_header(res, "X-Deleted", "9"));
    }

    SUBCASE("GET /items/hits -- controller state survives requests") {
        struct greeting {
            std::string message{};
            int doubled{};
            long long sum{};
        };
        const auto first = fut::parse<greeting>(
            run(ioc, rt, fut::method::GET, "/api/demo/items/hits").body);
        const auto second = fut::parse<greeting>(
            run(ioc, rt, fut::method::GET, "/api/demo/items/hits").body);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(second->sum > first->sum);
    }

    SUBCASE("GET /items/0 -- a named error helper picks the status") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/items/0");
        CHECK(res.status == 404);
        CHECK(res.body.find("no item 0") != std::string::npos);
    }

    SUBCASE("GET /nested/ping -- a controller one namespace deeper") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/nested/ping");
        CHECK(res.status == 200);
        CHECK(res.body.find("nested") != std::string::npos);
    }

    SUBCASE("GET /io -- the injected io_context is the live server loop") {
        struct io_out {
            bool injected{};
            bool timer_fired{};
        };
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/io");
        CHECK(res.status == 200);
        const auto v = fut::parse<io_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->injected);
        CHECK(v->timer_fired);
    }

    SUBCASE("GET /log -- the injected group writes into the server's chains") {
        struct log_out {
            bool logged{};
            std::string group{};
        };
        captured().clear();
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/log");
        CHECK(res.status == 200);
        const auto v = fut::parse<log_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->logged);
        CHECK(v->group == "demo.log");
        REQUIRE(!captured().empty());
        CHECK(contains(captured(), "demo.log", "demo /log was called"));
        CHECK(contains(captured(), "demo.log", "and this one is a warning"));
        // the explicitly named group is a separate one
        CHECK(contains(captured(), "demo.audit", "audited by the explicitly named group"));
    }

    SUBCASE("GET /audit-group -- an explicit name is used verbatim") {
        struct log_out {
            bool logged{};
            std::string group{};
        };
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/audit-group");
        CHECK(res.status == 200);
        const auto v = fut::parse<log_out>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->logged);
        CHECK(v->group == "demo.audit");
    }

    SUBCASE("GET /error/{kind} -- the whole mapping table") {
        struct expected {
            const char* kind;
            unsigned status;
            const char* message;
        };
        const expected cases[] = {
            {"400", 400, "the client sent something unusable"},
            {"401", 401, "no token on this route"},
            {"403", 403, "this is not yours"},
            {"404", 404, "nothing lives at that path"},
            {"409", 409, "that already exists"},
            {"418", 418, "i am a teapot"},
        };
        for (const auto& c : cases) {
            const auto res = run(ioc, rt, fut::method::GET,
                                 std::string("/api/demo/error/") + c.kind);
            CHECK(res.status == c.status);
            CHECK(res.body.find(c.message) != std::string::npos);
        }

        // an unexpected exception is a 500, and the detail stays out of the body
        const auto boom = run(ioc, rt, fut::method::GET, "/api/demo/error/500");
        CHECK(boom.status == 500);
        CHECK(boom.body.find("must not leak") == std::string::npos);
        CHECK(boom.body.find("internal server error") != std::string::npos);
    }

    SUBCASE("no binding at all -- the request object carries everything") {
        const std::pair<std::string_view, std::string_view> headers[] = {
            {"X-Demo-Token", "t0k3n"}, {"User-Agent", "raw-test"}};
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/raw/echo", "", "user=yue",
                             headers);
        CHECK(res.status == 200);

        struct echo {
            std::string method{};
            std::string path{};
            std::string user{};
            std::string agent{};
            bool has_body{};
            std::size_t headers{};
            std::string remote{};
        };
        const auto v = fut::parse<echo>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->method == "GET");
        CHECK(v->path == "/api/demo/raw/echo");
        CHECK(v->user == "yue");            // from the query, decoded by hand
        CHECK(v->agent == "raw-test");      // from a header
        CHECK(v->has_body == false);
        CHECK(v->headers == 2);
    }

    SUBCASE("no binding at all -- path, query, header and JSON body by hand") {
        const std::pair<std::string_view, std::string_view> headers[] = {
            {"X-Demo-Token", "abc"}};
        const auto res = run(ioc, rt, fut::method::POST, "/api/demo/raw/items/7",
                             R"({"name":"widget","quantity":3})", "trace=xyz", headers);
        CHECK(res.status == 201);

        struct item {
            long long id{};
            std::string name{};
            int quantity{};
        };
        const auto v = fut::parse<item>(res.body);
        REQUIRE(v.has_value());
        CHECK(v->id == 7);              // from the path
        CHECK(v->name == "widget");     // from req.json<item>()
        CHECK(v->quantity == 3);
        CHECK(has_header(res, "X-Trace", "xyz"));
        CHECK(has_header(res, "X-Token-Seen", "yes"));
    }

    SUBCASE("no binding at all -- a hand-built response") {
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/raw/created");
        CHECK(res.status == 201);
        CHECK(res.content_type.find("text/html") != std::string::npos);
        CHECK(has_header(res, "X-Demo-Created", "1"));
        CHECK(has_header(res, "Set-Cookie"));
        CHECK(res.body.find("created") != std::string::npos);
    }

    SUBCASE("streaming: SSE frames arrive one chunk at a time") {
        std::string head_type, body, finished;
        fut::stream s{
            [&](unsigned, std::string_view ct, const auto&) -> asio::awaitable<void> {
                head_type.assign(ct);
                co_return;
            },
            [&](std::string_view bytes) -> asio::awaitable<void> {
                body.append(bytes);
                co_return;
            },
            [&]() -> asio::awaitable<void> {
                finished = "yes";
                co_return;
            },
            [](std::chrono::milliseconds) -> asio::awaitable<void> { co_return; }};

        fut::request req{"GET", "/api/demo/events", "/api/demo/events", {}, {}, {}};
        ioc.restart();
        asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
            co_await rt.dispatch(req, s);
            co_await s.finish();
        }, asio::detached);
        ioc.run();

        CHECK(s.started());
        CHECK(head_type == "text/event-stream");
        CHECK(body.find("event: tick\ndata: 0\n\n") != std::string::npos);
        CHECK(body.find("event: tick\ndata: 4\n\n") != std::string::npos);
        CHECK(finished == "yes");
    }

    SUBCASE("streaming: a plain chunked body") {
        std::string body;
        fut::stream s{
            [](unsigned, std::string_view, const auto&) -> asio::awaitable<void> { co_return; },
            [&](std::string_view bytes) -> asio::awaitable<void> {
                body.append(bytes);
                co_return;
            },
            []() -> asio::awaitable<void> { co_return; },
            [](std::chrono::milliseconds) -> asio::awaitable<void> { co_return; }};

        fut::request req{"GET", "/api/demo/stream", "/api/demo/stream", {}, {}, {}};
        ioc.restart();
        asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
            co_await rt.dispatch(req, s);
        }, asio::detached);
        ioc.run();

        CHECK(body == "chunk-0\nchunk-1\nchunk-2\n");
    }

    SUBCASE("app middleware filters /secure and stamps what comes back") {
        // no token: the middleware answers 401 without reaching the handler,
        // and the outer middleware still stamps the response
        const auto denied = run(ioc, rt, fut::method::GET, "/api/demo/secure/ping");
        CHECK(denied.status == 401);
        CHECK(denied.body.find("missing X-Demo-Token") != std::string::npos);
        CHECK(has_header(denied, "X-Demo-App", "yes"));

        // with the token the handler runs
        const std::pair<std::string_view, std::string_view> headers[] = {
            {"X-Demo-Token", "let-me-in"}};
        const auto ok = run(ioc, rt, fut::method::GET, "/api/demo/secure/ping", "", "", headers);
        CHECK(ok.status == 200);
        CHECK(ok.body.find("secure") != std::string::npos);
    }

    SUBCASE("a middleware that throws is mapped like a handler error") {
        const std::pair<std::string_view, std::string_view> headers[] = {{"X-Demo-Burst", "1"}};
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/ping", "", "", headers);
        CHECK(res.status == 429);
        CHECK(res.body.find("rate limiting") != std::string::npos);
    }

    SUBCASE("app middleware stamps the app's routes only") {
        const auto ok = run(ioc, rt, fut::method::GET, "/api/demo/ping");
        CHECK(has_header(ok, "X-Demo-App", "yes"));
        CHECK(has_header(ok, "X-Platform", "1"));

        // a 404 belongs to no app, so the app middleware does not run
        const auto missing = run(ioc, rt, fut::method::GET, "/api/demo/nope");
        CHECK(missing.status == 404);
        CHECK(!has_header(missing, "X-Demo-App"));
        CHECK(has_header(missing, "X-Platform", "1"));   // platform sees it
    }

    SUBCASE("platform middleware can short-circuit") {
        const std::pair<std::string_view, std::string_view> headers[] = {{"x-block", "1"}};
        const auto res = run(ioc, rt, fut::method::GET, "/api/demo/ping", "", "", headers);
        CHECK(res.status == 401);
        CHECK(res.body.find("blocked") != std::string::npos);
    }

    SUBCASE("unknown method on a known path is a 405") {
        const auto res = run(ioc, rt, fut::method::DEL, "/api/demo/ping");
        CHECK(res.status == 405);
    }
}

} // TEST_SUITE
