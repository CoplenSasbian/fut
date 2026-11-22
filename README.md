# fut

C++26 coroutine web-server library: a small server executable plus **self-registering app DLLs**.

Business logic lives in one DLL per URL prefix; deploying an update means rebuilding that DLL and
swapping the file — the server and every other app stay untouched.

An app DLL exports exactly one function, `fut_app_register(fut::fut_ctx*)`. Through the context it
reaches the server's services ("modules") without linking against the executable, and registers its
routes into the server's router. Which annotations produce those routes is the app's business: the
scanner only knows *"an annotation derived from `fut::scannable` has a member `operation()`"* --
it calls it on the annotation object with the context.

Reflection is a **hard requirement**: the library is built on `std::meta` (P2996) and has no
reflection-free fallback path.

## Features

* Asio coroutines (`asio::awaitable`) for I/O; HTTP/1.1 messages parsed by **llhttp** and
  serialized by ~40 lines of our own code (no Boost.Beast)
* Annotation-declared routes: `[[=fut::http_get("/ping")]]` on controller methods
* Automatic parameter binding: JSON body DTOs, query/path/header values, form fields,
  multipart file uploads, raw request escape hatch
* Return-type adaptation: `fut::response` passthrough, **plain strings served as text/plain**,
  glaze-serializable DTOs as JSON, anything else is a **compile error**
* One error mapping for every app: throw `fut::http_error` (or a helper) and the router turns it
  into the status and body; the executable can reshape the envelope
* One DLL per app (Django-style); registration through a context, no exported tables
* Generic scanner: adding a new scannable annotation changes neither the scanner nor CMake
* One `fut::exec::server` object for the host executable: configure it in a chain, then `run()`
* Asynchronous logging: colour console + rotating file chains, reflection-based level names
* doctest suite with full end-to-end coverage (loads the real DLLs)

## The HTTP message layer

Only two things are needed from an HTTP library: parse a request into method / target / headers /
body, and write a response. Beast does both, but it brings a large template surface (and the
compile time that goes with it) for those two jobs — and nothing else in the server needs it:
Asio owns the sockets, and Boost's database/Redis libraries integrate with Asio's executor, not
with Beast. So the message layer is:

| Concern | Choice |
| --- | --- |
| Parsing | **llhttp** — the parser Node.js uses: small C library, incremental, fuzz-tested, handles Content-Length, chunked bodies, keep-alive semantics |
| Serialization | `core/src/http_message.cpp` — status line, framing headers, body (~40 lines) |
| I/O, coroutines, timers | Boost.Asio (unchanged) |

```
socket ──read──▶ std::string buffer ──▶ request_parser (llhttp) ──▶ fut::exec::http_request_message
                                              │                         │
                                     pipelined tail stays       dispatch -> fut::response
                                              ▼
                       serialize_response(...) ──write──▶ socket
```

The parser is incremental and reports how many bytes a message used, so pipelined requests work
without copying, and a partial read simply returns `need_more`. Per connection, the read buffer, the
serialized-response buffer and the parser's message are all **reused**. Header names and values live
back-to-back in one `std::string` (the views in `message.headers` and in `fut::request.headers`
point into it), so after the first request a keep-alive connection stops allocating for the request
line, the headers and the response envelope. The views are materialised after the header block stops
growing, so a reallocation cannot leave one dangling. Walking the middleware chain allocates nothing either — the
`next_fn` captures one pointer, so it stays inside `std::function`'s small-buffer optimization.
Timeouts use an `asio::steady_timer`
that closes the socket, and oversized bodies are rejected with 413.

## Requirements (hard)

| Component | Requirement |
| --- | --- |
| Compiler | **GCC >= 15 with `-freflection`** (std::meta reflection; no fallback path) |
| Dependencies | vcpkg: `boost-asio`, `llhttp`, `glaze`, `doctest` |
| Build | CMake >= 3.25 + Ninja |
| Triplet | `x64-mingw-dynamic` |

The top-level `CMakeLists.txt` fails configuration with a clear message when the toolchain cannot
provide `std::meta`; the public headers `#error` as a second line of defense.

## Getting started

Set two environment variables (adjust to your machine):

```bash
export MINGW_ROOT=/d/mingw64        # GCC 15+ toolchain root
export VCPKG_ROOT=/d/vcpkg/vcpkg    # a vcpkg checkout
```

Build and test:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

**In an IDE** (CLion, VS Code, ...) select a project preset — `CMakePresets.json` already carries
the compiler, Ninja and `VCPKG_TARGET_TRIPLET=x64-mingw-dynamic`, which is the part that is easy to
get wrong. Configuring by hand is of course supported; then just pass the same two values your
preset would:

```
-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic
```

(The triplet must match the compiler ABI: mixing MinGW with MSVC-built vcpkg packages fails at link
time with `undefined reference to 'fprintf'` / `_RTC_InitBase` / `__chkstk`.)

Run (from the repository root, which holds `config.json`):

```bash
./build/release/bin/fut_server.exe          # or: --port 9000 --threads 4 --log-level debug
```

Artifacts (`build/<preset>/bin`, self-contained — runtime DLLs included):

```
bin/
├── fut_server.exe
├── libstdc++-6.dll  libgcc_s_seh-1.dll  libwinpthread-1.dll
├── test_*.exe
└── apps/
    └── fut_app_demo.dll
```

```bash
curl http://127.0.0.1:8080/api/core/status
curl http://127.0.0.1:8080/api/demo/ping                  # {"pong":true,"app":"demo"}
curl "http://127.0.0.1:8080/api/demo/query?name=yue&count=4"   # {"message":"yue!","doubled":8,"sum":10}
curl -X POST -d '{"message":"hi","count":4}' http://127.0.0.1:8080/api/demo/body
curl -X PUT  -d '{"name":"widget","quantity":3}' http://127.0.0.1:8080/api/demo/items/7
curl -X DELETE -w '%{http_code}\n' http://127.0.0.1:8080/api/demo/items/9   # 204
curl http://127.0.0.1:8080/api/demo/info                  # the reflected route list
curl http://127.0.0.1:8080/api/demo/text                  # plain text, text/plain
```

## How an app is loaded

```
server                                        app DLL
  LoadLibrary("fut_app_demo.dll")
  ctx.app_name/app_prefix set from the exports
  fut_app_register(ctx)  ───────────────────▶  scan::run<^^demo>(ctx)
                                                  │
  ctx.get_module("router")  ◀───────────────────┤  find [[=fut::controller]] classes
   -> void*, cast to router_module*              │  call annotation.operation<Class>(ctx)
                                                  ▼
  router.add_route(self, GET, "/api/demo/ping", &fn)
```

* The server owns the router; a DLL never links against the executable.
* `fut_ctx` is the whole contract between them: a module lookup plus a server-private `host`.
* Modules are stored under **two keys**: their interface type (`typeid`) and a unique name. Lookup is
  by type by default — `ctx->get_module<fut::http::router_module>()` — and the name only matters when
  several modules share a type (`ctx->get_module<store>("archive")`). Asking for a type that more
  than one module answers, without a name, throws `fut::module_error`: an ambiguous lookup is a
  configuration bug, and picking one arbitrarily would hide it. `get_module("name")` still returns
  an untyped `void*` for hand-written modules.
* `ctx.app_name` / `ctx.app_prefix` are the app's identity, filled by the server from the DLL's
  exports before registration -- which is how annotations compose absolute paths without the
  scanner ever carrying a prefix argument.
* The loader checks `fut_abi_version()` and skips mismatched DLLs; `fut_app_name()` /
  `fut_app_prefix()` are used for logging and `/api/core/status`.

## Writing an app

An app is a Django-style unit: a name, a URL prefix, and controller classes in the namespace named
after the app. `apps/demo` is the reference implementation — one DLL that exercises every feature:

```
apps/demo/
├── CMakeLists.txt          add_app(demo /api/demo demo.hpp ...*.cpp)
├── demo.hpp                umbrella: the ONLY header the scan includes
├── common.hpp              shared preamble (asio alias, fut/http, inject headers)
├── dto.hpp                 plain aggregates; declares all_routes()
├── services.hpp / .cpp     injected io_context and log groups (extern decl + def)
├── middleware.hpp / .cpp   two app filters: rewriting + filtering/throwing
├── routes.hpp / .cpp       collect<T>() template; all_routes() body
└── routes/
    ├── core.hpp     / .cpp      GET ping/info/text/custom/query/header/request/secure/ping
    ├── binding.hpp  / .cpp      POST body / body-scalar / form / upload
    ├── runtime.hpp  / .cpp      GET io/log/audit-group/error/{kind}
    ├── items.hpp    / .cpp      /items: hits, get, put, delete (stateful)
    ├── nested.hpp   / .cpp      a controller one namespace deeper
    └── raw.hpp      / .cpp      three routes with no binding at all
```

**Declarations and definitions are split**, which is the layout to copy for a real app:

* the **headers** carry the annotated declarations — controllers, the injected services, the
  middleware registration — and the umbrella includes them in a fixed order;
* the **`.cpp` files** carry the bodies and no attributes at all.

The scan only sees the umbrella's include chain, so the generated unit compiles declarations while
every `.cpp` is an ordinary definition unit linked into the same DLL. (Header-only apps are still
fine for something tiny: list just the header and define the actions inline.)

```cpp
// apps/demo/routes/binding.hpp -- declarations, annotations here
#pragma once
#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller{}]] binding_controller {

    [[=fut::http_post("/body")]]                       // -> POST /api/demo/body
    asio::awaitable<greeting_out> body([[=fut::from_body{}]] greeting_in in);

    [[=fut::http_post("/upload")]]                     // -> POST /api/demo/upload
    asio::awaitable<upload_out> upload([[=fut::from_form{}]] std::string title,
                                       [[=fut::from_file{}]] fut::uploaded_file file);
};

} // namespace demo
```

```cpp
// apps/demo/routes/binding.cpp -- bodies, no attributes
#include "binding.hpp"

namespace demo {

asio::awaitable<greeting_out> binding_controller::body(greeting_in in) {
    co_return greeting_out{in.message, in.count * 2, in.count * (in.count + 1) / 2};
}

} // namespace demo
```

Injected services follow the same rule: the annotation sits on an `extern` declaration in the
header (that is the entity the scan sees), and the definition in the `.cpp` has no attribute:

```cpp
// services.hpp
[[=fut::inject{}]] extern boost::asio::io_context* g_io;
[[=fut::log_group{"demo.audit"}]] extern fut::log::group_ref audit_log;

// services.cpp
boost::asio::io_context* g_io = nullptr;
fut::log::group_ref audit_log{};
```

Wire it up with the `add_app()` helper (defined in `cmake/fut.cmake`, included by the top-level
`CMakeLists.txt`):

```cmake
# apps/demo/CMakeLists.txt -- the umbrella header plus the definition units.
add_app(demo /api/demo
    demo.hpp
    services.cpp
    middleware.cpp
    routes.cpp
    routes/core.cpp
    routes/binding.cpp
    routes/runtime.cpp
    routes/items.cpp
    routes/nested.cpp
)
```

 `fut_app_<name>` MODULE target, places
`fut_app_<name>.dll` into `bin/apps`, adds the include path and the standard link set
(`Boost::asio`, `glaze::glaze`), defines `FUT_APP_NAME` / `FUT_APP_PREFIX` as quoted literals, and
validates the prefix at configure time (must start with `/`, must not end with `/`).

### The generated export unit

CMake writes one translation unit per app at configure time:

```cpp
// Generated by fut's add_app(); do not edit.
#include <fut/app.hpp>          // generic entry: scan contract + export macro

#include <demo.hpp>

FUT_EXPORT_APP_NS(FUT_APP_NAME, FUT_APP_PREFIX, ^^demo)
```

`<fut/app.hpp>` is generic — it knows that a module is scanned and that the executable calls one
function, nothing about HTTP. The app's own header includes `<fut/http/http.hpp>` because it uses
`fut::controller`; an app that registers something else never includes the HTTP headers at all.

`FUT_EXPORT_APP_NS` expands to:

```cpp
extern "C" {
std::uint32_t fut_abi_version();
const char*   fut_app_name();
const char*   fut_app_prefix();
void          fut_app_register(fut::fut_ctx* ctx) {
    fut::scan::run<^^demo>(ctx);
}
}
```

**The prefix comes from the macro** -- as the app's identity, not as a scanner argument. It is baked
in as the `FUT_APP_PREFIX` compile definition from the `add_app()` call, exported by
`fut_app_prefix()`, and placed into `ctx.app_prefix` by the server before it calls
`fut_app_register`. Annotations read it from the context and compose absolute paths; the scanner
never learns that a prefix exists.

### Scan rule: one level, namespaces only

```
for each DIRECT member of the namespace:
    if it is a namespace      -> recurse into it
    otherwise                 -> call every scannable annotation found ON it, once, never descend
```

The call is made on the annotation object itself, with the hook located by reflection:

```cpp
constexpr auto self = [: std::meta::constant_of(annotation) :];
template for (constexpr auto m : members_of(^^decltype(self)))   // find "operation"
    if constexpr (is_hook(m))
        self.template [: m :]<Entity>(ctx);   // or self.[: m :](ctx) when it is not a template
```

So a controller in a nested namespace **is** found, while a controller class nested inside another
class is **not** — nothing but a namespace is ever recursed into. A namespace member that cannot be
queried (an unresolved overload set, for example) carries no annotation and is skipped.

### One scanner (why headers included elsewhere cannot duplicate anything)

Scanning code exists in exactly **one** place per app: the generated export unit. `members_of` is an
ordinary library call, not a compiler hook — including the app header from `a.cpp`, `b.cpp` or any
other target puts no scanning code there, so nothing is registered twice. Duplicates are
structurally impossible, not merely discouraged.

Two app layouts are supported, picked per source extension:

**Declaration/definition split (recommended for real apps).** Annotated *declarations* in the
header, plain definitions in `.cpp` files — reflection works on declarations, no definition has to be
visible to the scanner; the generated unit's calls resolve through the linker.

**Header-only (what `apps/demo` does).** List only the umbrella header and define the actions
inline in the modules it includes; the generated unit includes them — one scan, one definition.

**Annotation placement is enforced.** The scan sees exactly what the listed headers declare, so
route annotations have exactly one legal home:

| Where the annotation sits | Result |
| --- | --- |
| on a declaration in a listed header | scanned — the canonical place |
| only in a `.cpp` (on a definition) | **configure fails** (`add_app` greps listed `.cpp` files — a scan-blind annotation would otherwise be a silently missing route) |

Headers are unity-included into the generated unit (several headers → all scanned); `.cpp` sources
are compiled normally and linked into the same DLL.

### Controllers

A controller is the scannable annotation: a class annotated in the standard class-head position
(AFTER the `struct` keyword). Its `operation()` walks the class's member functions, reads the verb
annotation on each, and registers one route per method. The route path is composed from three parts:

```
add_app(demo /api/demo ...)     ->  "/api/demo"      (compile-time macro)
[[=fut::controller("/users")]]  ->  "/users"         (class annotation)
[[=fut::http_get("/{id}/name")]]->  "/{id}/name"     (method annotation)
                                    => /api/demo/items/{id}
```

One instance per controller type is created lazily per DLL (a thread-safe magic static), so state
persists across requests.

```cpp
namespace demo {

struct [[=fut::controller("/items")]] items_controller {
    std::atomic<std::uint64_t> hits{0};

    [[=fut::http_get("/hits")]]                        // -> GET /api/demo/items/hits
    asio::awaitable<greeting_out> hits_count() {
        co_return greeting_out{"hits", 1, hits.fetch_add(1, std::memory_order_relaxed)};
    }

    [[=fut::http_get("/{id}")]]                        // -> GET /api/demo/items/{id}
    asio::awaitable<item> get([[=fut::from_path("id")]] long long id) {
        co_return item{id, "item-" + std::to_string(id), 1};
    }
};

} // namespace demo
```

Member actions must be public (the invoke splices through them); parameter binding and return-type
adaptation are identical everywhere.

### Verbs

`http_action{path, method}` is the verb data type; `http_get()` and friends are `constexpr`
functions returning values you can use as annotations:

```cpp
[[=fut::http_get("/ping")]]     [[=fut::http_post("/echo")]]
[[=fut::http_put("/item")]]     [[=fut::http_delete("/item")]]
```

`fut::method` is an enum (`GET`, `POST`, `PUT`, `DEL` — `DEL` because `DELETE` is a Windows macro).

## Adding a new scannable annotation

Derive `fut::scannable` and give it a member `operation`; nothing else changes. The scanner
finds the hook **by reflection** — it reflects the annotation type's members, takes the one named
`operation` and splices the call — so two shapes are accepted:

```cpp
struct my_annotation : fut::scannable {
    int priority = 0;

    template <std::meta::info Entity>                 // entity-aware
    void operation(fut::fut_ctx* ctx) const {
        // `this` is the annotation value attached to Entity (priority, path, ...);
        // Entity is the class the annotation sits on; ctx gives the app identity
        // and any server module.
    }
};

struct my_app_annotation : fut::scannable {
    void operation(fut::fut_ctx* ctx) const {         // entity-agnostic
        // app-wide work that does not depend on the annotated class
    }
};

struct [[=fut::my_annotation{.priority = 5}]] some_class { ... };
struct [[=my_app_annotation{}]] app_marker { ... };
```

An annotation with no hook is a **compile error**, not a silent no-op:

```
error: static assertion failed: fut: a scannable annotation needs an 'operation' hook -- either
'template <std::meta::info Entity> void operation(fut::fut_ctx*) const' (entity-aware) or
'void operation(fut::fut_ctx*) const' (entity-agnostic)
```

(GCC reflects a member function *template* as `is_template == true`, `is_function == false`; that
is how the two shapes are told apart.)

The scanner needs no change, `add_app` needs no change, and the new annotation can live in any
module (including one that never includes the HTTP headers).

### Composing annotations

**One annotation per concern, stacked on the entity.** The scanner calls *every* scannable
annotation on an entity, in declaration order, so composition is adding another annotation — never
subclassing an existing one:

```cpp
struct [[=fut::controller("/mon")]] [[=health{}]] monitored_controller {
    [[=fut::http_get("/ping")]]
    asio::awaitable<greet_out> ping();        // controller registers /mon/ping
};                                            // health        registers /mon/health
```

**Annotations are peers, not a hierarchy.** When one needs data another carries, it reads it from
the shared entity — the entity is the composition context:

```cpp
struct health : fut::scannable {
    template <std::meta::info Entity>
    void operation(fut::fut_ctx* ctx) const {
        constexpr auto controller_ann = fut::ann::first<fut::controller>(Entity);
        if constexpr (controller_ann != std::meta::info{}) {
            constexpr auto controller_value = [: std::meta::constant_of(controller_ann) :];
            const fut::http::router_module* rm = fut::http::router_of(ctx);
            rm->add_route(rm->self, fut::method::GET,
                          fut::join_path(ctx->app_prefix,
                                         controller_value.path.view(), "/health"),
                          &health_route);
        }
    }
};
```

`fut::ann` (`fut/annotations.hpp`) is the generic query layer: `of<T>(entity)`, `first<T>(entity)`,
`has<T>(entity)`, cv-agnostic (GCC records a constexpr-function annotation value as `const T`).

**Deriving from a concrete annotation is not the way to compose.** It looks convenient — the
derived annotation calls `controller::operation<Entity>(ctx)` and adds its own route — but it
makes one annotation depend on another's implementation, hides the call order inside that call,
and stops composing as soon as two independent behaviours are wanted (multiple inheritance then
re-introduces the ordering problem at every call site). Note also that `operation` cannot be
virtual at all: virtual function templates are ill-formed, and a polymorphic annotation value
cannot be read back (`reflect_constant failed`). Stacking needs neither.

**Middleware is a different concern.** "Do auth / rate-limit around every handler" is not an
annotation-composition problem: annotations describe *which routes exist*, while invocation policy
wraps a `route_fn`. Put that in the router/invoke layer, not in an annotation's `operation`.

## Without binding: the request and the response

Binding annotations are a convenience, not a requirement. Take the request and read it; build the
response and return it:

```cpp
[[=fut::http_post("/raw/items/{id}")]]
asio::awaitable<fut::response> create(const fut::request& req,
                                      const fut::path_match& params) {
    const std::string_view id = params.segment("id");          // path
    const auto body = req.json<item>();                        // body -> DTO
    const std::string trace = req.param_or("trace", "none");   // query, decoded
    const std::string token{req.header_or("X-Demo-Token", "")}; // header

    fut::response res = fut::json(*body, 201);
    res.set_header("X-Trace", trace).set_cookie("session", "abc");
    co_return res;
}
```

**`const fut::request&`** — everything about the call:

| Read | Returns |
| --- | --- |
| `method` / `path` / `query` / `target` / `body` / `remote` | `std::string_view` (remote is `ip:port`) |
| `headers` | span of `(name, value)` |
| `header(name)` / `has_header(name)` / `header_or(name, fallback)` | case-insensitive |
| `content_type()` | `Content-Type`, empty when absent |
| `verb()` / `is(fut::method::GET)` | the parsed method |
| `param(name)` → `optional<string>`, `param_or`, `has_param`, `params()` | query, percent-decoded |
| `json<T>()` → `optional<T>` | the body parsed as a DTO |
| `text()` / `empty_body()` | the raw body |

**`const fut::path_match&`** — the captured segments: `segment(name)`, `has(name)`, `count()`,
`params()`. Any action may take it, with or without binding annotations.

**`fut::response`** — status, media type, headers, body:

| Build | Result |
| --- | --- |
| `fut::response::json(payload, code)` / `::text(..)` / `::html(..)` | media type set for you |
| `::no_content()` / `::redirect(location, code)` | 204 / 302 with `Location` |
| `set_status` / `set_body` / `set_type` | chainable |
| `set_header` (replace) / `add_header` (append) / `erase_header` | chainable |
| `set_cookie(name, value, attrs)` | appends a `Set-Cookie` |
| `header(name)` / `has_header(name)` / `ok()` | read back |

```bash
curl -H "User-Agent: curl-raw" "http://127.0.0.1:8080/api/demo/raw/echo?user=yue"
# {"method":"GET","path":"/api/demo/raw/echo","user":"yue","agent":"curl-raw",
#  "has_body":false,"headers":3,"remote":"127.0.0.1:60126"}

curl -X POST -H "X-Demo-Token: abc" -d '{"name":"widget","quantity":3}'  "
  "http://127.0.0.1:8080/api/demo/raw/items/7?trace=xyz"
# HTTP/1.1 201 Created
# X-Trace: xyz
# {"id":7,"name":"widget","quantity":3}
```

`apps/demo/routes/raw.*` is exactly this: three routes, no binding annotation anywhere.

## Middleware and filters

Middleware is a coroutine that either answers the request or continues the chain:

```cpp
asio::awaitable<fut::response> log_request(const fut::request& req,
                                          const fut::path_match& params,
                                          const fut::http::next_fn& next) {
    const auto start = std::chrono::steady_clock::now();
    fut::response res = co_await next(req, params);      // or return early
    res.headers.emplace_back("X-Elapsed-Us", std::to_string(micros_since(start)));
    co_return res;
}
```

**Two scopes, deliberately separate.** An app never reaches outside itself:

| Scope | Registered by | Covers | Example |
| --- | --- | --- | --- |
| **platform** | the executable (`fut::exec::server::add_middleware`) | every request of every app, *including 404s* | request log, metrics, CORS, global auth |
| **app** | one app DLL (`router_module::add_middleware` while it loads) | that app's routes only | tenant check, app-specific headers |

```cpp
// executable: platform scope
fut::exec::server().add_middleware(&log_request).run();

// app DLL: app scope -- fut::filter is the library's annotation, no
// registration code in the app at all
namespace demo {
[[=fut::filter{}]]
asio::awaitable<fut::response> stamp(const fut::request& req,
                                     const fut::path_match& params,
                                     const fut::http::next_fn& next) {
    fut::response res = co_await next(req, params);
    res.headers.emplace_back("X-Demo-App", "yes");
    co_return res;
}
} // namespace demo
```

`fut::filter` enforces the signature at compile time (return type and parameter types), and
declaration order is run order: the first filter declared is outermost. The demo app shows the
three shapes in `apps/demo/middleware.hpp` — **rewriting** (calls `next()`, adds headers),
**filtering** (answers 401 itself, never calling `next()`), and **failing** (throws
`fut::http_error`, which the router maps like a handler error).

**Why not cross-app?** Apps are independent deployment units: an app's DLL is swapped without
rebuilding anything else, and a middleware registered by one app must not silently change another
app's behaviour. Platform-wide policy therefore belongs to the executable, and the router keeps the
two chains apart.

**Order** is platform (registration order, outermost) → matching → app (registration order) → the
route handler. A middleware may short-circuit (returning a `fut::response` without calling
`next`), rewrite the response, or both:

```bash
curl -D- -o /dev/null http://127.0.0.1:8080/api/demo/ping
# HTTP/1.1 200 OK
# X-Demo-App: yes              <- app middleware (rewriting)
# X-Demo-Elapsed-Us: 25        <- app middleware, timed

curl -w ' [%{http_code}]' http://127.0.0.1:8080/api/demo/secure/ping
# {"error":"missing X-Demo-Token"} [401]      <- app middleware (filtering, handler never ran)

curl -H 'X-Demo-Burst: 1' -w ' [%{http_code}]' http://127.0.0.1:8080/api/demo/ping
# {"error":"the demo is rate limiting this request"} [429]   <- middleware threw
```

Two implementation notes:

* the chains are assembled by `router::finalize()` once, after every app is loaded, so dispatch
  costs one lookup per request;
* an app middleware crosses the DLL boundary as a **function pointer** (`fut::http::middleware_hook`),
  not as a `std::function`: a callable constructed inside a DLL would otherwise be destroyed after
  the DLL is unloaded. The executable wraps the pointer in its own `std::function`.

## Errors

An action is a coroutine, so **throwing is the way out**. The router is the single place that turns
an exception into a response, which is why every app reports failures the same way:

| What happened | Status | Body (default) |
| --- | --- | --- |
| `throw fut::http_error(418, "msg")` | the thrown status | `{"error":"msg"}` |
| `throw fut::not_found("msg")` (and the other helpers) | 404 | `{"error":"msg"}` |
| `throw fut::bind_error("msg")` (bad body/query/path/header/form) | 400 | `{"error":"msg"}` |
| any other `std::exception` | 500 | `{"error":"internal server error"}` — the detail goes to the log, never to the client |
| a middleware throws | same mapping | — |
| method unknown | 405 | `{"error":"method not allowed"}` |
| path exists under another method | 405 | `{"error":"method not allowed"}` |
| no route at all | 404 | `{"error":"no route"}` |

```cpp
[[=fut::http_get("/fail/{code}")]]
asio::awaitable<pong_dto> fail([[=fut::from_path("code")]] int code) {
    throw fut::http_error(static_cast<unsigned>(code), "requested failure");
}
```

```bash
curl -w " [%{http_code}]" http://127.0.0.1:8080/api/demo/fail/418
# {"error":"requested failure 418"} [418]
```

The demo app publishes the whole table as one route — `/api/demo/error/{kind}` with
`400`/`401`/`403`/`404`/`409`/`418`/`500` — so every row below is one curl away.

Helpers: `bad_request`, `unauthorized`, `forbidden`, `not_found`, `method_not_allowed`,
`conflict`, `payload_too_large`, `unsupported_media_type`, `unprocessable`, `too_many_requests`,
`internal_error`, `unavailable` (all in `fut/http/error.hpp`).

**Reshaping the envelope** is platform policy, not an app's:

```cpp
fut::exec::server()
    .on_error([](unsigned status, std::string_view message) {
        fut::response res;
        res.status = status;
        res.body = std::string{"{\"code\":"} + std::to_string(status) +
                   ",\"detail\":\"" + std::string(message) + "\"}";
        return res;
    })
    .run();
```

An app that needs its own error shape wraps the response in an app middleware instead (it can
`try`/`catch` around `co_await next(...)`, since middleware is part of the same chain).

## Services and injection

The context also publishes *services*: objects the server owns and an app may need. They are
registered under a type and a name (see *How an app is loaded*), and the executable adds its own
with `server_builder::add_module`:

```cpp
fut::exec::server().add_module("cache", &cache)   // keyed by typeid(cache)
                 .add_module("primary", &store_a)  // two stores: the name disambiguates
                 .add_module("archive", &store_b)
                 .run();
```

The io_context is published automatically, so an action can post work or start a timer on the
server's loop:

```cpp
namespace demo {
[[=fut::inject{}]] boost::asio::io_context* g_io = nullptr;   // assigned while the app loads
}

[[=fut::http_get("/io")]]
asio::awaitable<io_dto> io() {
    asio::steady_timer timer(*g_io);              // the server's own loop
    timer.expires_after(std::chrono::milliseconds(1));
    co_await timer.async_wait(asio::use_awaitable);
    co_return io_dto{true, true};
}
```

`fut::inject` (`fut/inject.hpp`) fills a namespace-scope **pointer** from the context; the pointee
type is the lookup key. Pointers only: a reference has to be bound at its declaration, which is a
moment the scanner does not control.

**Picking among modules of one type** — the annotation may carry a name, and the variable's own
name is the fallback:

```cpp
[[=fut::inject{}]] boost::asio::io_context* g_io = nullptr;   // one of its type: nothing needed

[[=fut::inject{"primary"}]] store* a = nullptr;               // the given name
[[=fut::inject{}]] store* archive = nullptr;                  // falls back to "archive"
```

The order is: type alone (unique → done) → the annotation's name → the variable's identifier. If
several modules share the type and neither name matches, registration fails with
`fut::module_error` rather than picking one. Missing modules are left as null.

The companion rule for logs: `fut::log_group{"name"}` uses that name verbatim, and only a bare
`fut::log_group{}` derives `app.variable`.

### Logging from an app

An app DLL cannot call `fut::log::submit` — that is the executable's log core — so it holds a
`fut::log::group_ref` wired to a service the server publishes:

```cpp
namespace demo {
[[=fut::log_group{}]] fut::log::group_ref log;                    // "demo.log"
[[=fut::log_group{"demo.audit"}]] fut::log::group_ref audit;      // "demo.audit" -- used as given
}

[[=fut::http_get("/log")]]
asio::awaitable<log_dto> write_log() {
    log.info("demo /log was called");             // lands in the server's chains
    co_return log_dto{true, std::string(log.name())};
}
```

```bash
curl -s http://127.0.0.1:8080/api/demo/log
# {"logged":true,"group":"demo.log"}
# console: 2026-11-22 16:06:40 [INFO    ] demo.log     demo /log was called
```

A name given to the annotation wins; with none, the name is the **app name plus the variable's
identifier**, so two apps can both have a `log` without colliding. The sink is the server's log service, so app lines share the chains, level
filter and formatters with the server's own — no second logging system in the DLL. `group_ref` is
header-only and only calls the service pointer; with no service published every call is a no-op,
which is what a unit test wants.

Two types, on purpose: `fut::log::group` (what `fut::log::grp()` returns) writes straight into
this module's core and is for the executable; `fut::log::group_ref` is the service-backed form that
crosses the DLL boundary.

Two lifetime rules, both enforced by tests:

* the `app_loader` must be declared **before** the router and the io_context, so the DLLs are
  unloaded last — the router holds handlers built from DLL code, and the io_context may still hold
  completions whose handlers live in a DLL;
* `io_context::running_in_this_thread()` is **not usable from inside a DLL**: asio tracks "am I
  inside run()" in a per-module thread-local, so the DLL reads a different copy and always gets
  `false`. Prove the loop is live by scheduling something on it (the timer above) instead.

## Parameter binding

| Parameter annotation | Source |
| --- | --- |
| `[[=fut::from_body{}]]` | request body JSON, parsed into the parameter type (glaze) |
| `[[=fut::from_query("k")]]` / `[[=fut::from_query{}]]` | query parameter (empty key -> parameter name); on a DTO, every member is filled by name |
| `[[=fut::from_path("a")]]` / `[[=fut::from_path{}]]` | `{placeholder}` segment of the path pattern |
| `[[=fut::from_header("X-K")]]` | request header |
| `[[=fut::from_form{}]]` / `[[=fut::from_form("k")]]` | form field (urlencoded or a multipart text part); on a DTO, every member is filled by name |
| `[[=fut::from_file{}]]` / `[[=fut::from_file("k")]]` | uploaded file (multipart part), type `fut::uploaded_file` |
| none, type `const fut::request&` | the raw request (escape hatch) |
| none, anything else | **compile error** (static_assert explains) |

Binding failures (invalid JSON, missing value) become **400** responses; unmatched routes become
**404**; an unknown HTTP method becomes **405**. Scalars are parsed by glaze.

### Forms and uploads

`application/x-www-form-urlencoded` and `multipart/form-data` are parsed by
`fut/http/form.hpp` — about 200 lines, header-only, no dependency. The body is parsed **once per
request**, and only when an action asks for a field of it:

```cpp
struct upload_out {
    std::string title{};
    std::string filename{};
    std::size_t size{};
    std::string content_type{};
};

struct [[=fut::controller{}]] demo_controller {
    [[=fut::http_post("/upload")]]
    asio::awaitable<upload_out> upload(
        [[=fut::from_form{}]] std::string title,          // <- form field "title"
        [[=fut::from_file{}]] fut::uploaded_file file);   // <- multipart file "file"
};
```

```bash
curl -F "title=quarterly report" -F "file=@notes.txt;type=text/plain" \
     http://127.0.0.1:8080/api/demo/upload
# {"title":"quarterly report","filename":"notes.txt","size":28,"content_type":"text/plain"}
```

* **A form or query set can fill a whole DTO.** When the parameter is an aggregate, the annotation
  matches members by name instead of looking for one field called like the parameter:

  ```cpp
  struct echo_in { std::string message{}; int count{0}; };

  [[=fut::http_post("/formecho")]]
  asio::awaitable<echo_out> form_echo([[=fut::from_form{}]] echo_in in);  // message=...&count=...
  ```

  Missing members keep the value they were default-built with (`count` stays 0), unknown names are
  ignored like extra JSON keys, and members must be scalars or `std::string`. The same applies to
  `from_query` on a GET: `?a=6&b=7` fills `struct add_in { int a; int b; }`.
* `fut::uploaded_file` carries `name`, `filename`, `content_type` and `data` — **four views into
  the request body**, not copies. Same for `form_data::fields`: multipart parsing borrows the
  bytes it was given, so a 30 MB upload stays one 30 MB buffer. The views live as long as the
  request does; copy (`std::string(field)`) what must outlive it. The one exception is
  `application/x-www-form-urlencoded`, where percent-decoding produces new bytes: those live in
  `form_data::owned` (a deque, so the views stay valid when the form is moved to the caller).
* Bodies above 32 MB are rejected with **413** before they reach an action.
* A field binds to `std::string` or to any scalar (`int`, `double`, ...) through the same
  conversion as the other bindings; an empty annotation key falls back to the parameter name.
* Both encodings are accepted for `from_form`; `from_file` needs multipart (a plain form has no
  files). A mismatched body is a **400** with a precise message (`missing uploaded file: file`,
  `form binding needs 'application/x-www-form-urlencoded' or 'multipart/form-data' (got ...)`).

### Return types

An action returns `asio::awaitable<T>` where `T` may be:

| `T` | Behavior |
| --- | --- |
| `fut::response` | passed through unchanged |
| `std::string` | served directly as the response body (`text/plain`) |
| glaze-serializable DTO | serialized to JSON automatically |
| anything else | **compile error** (static_assert explains) |

### Route matching

Patterns are complete paths (the DLL composes its own prefix). Matching prefers the **most specific**
route: the pattern with the most literal segments wins, and registration order breaks ties, so
`/users/hits` beats `/users/{id}`.

## The server

The host executable is a thin shell over `fut::exec::server`; embedders use it directly:

```cpp
int main() {
    return fut::exec::server()
        .config_file("config.json")   // JSON config; explicit values below override it
        .host("0.0.0.0")
        .port(8080)
        .threads(4)
        .apps_dir("apps")             // relative paths resolve against the executable
        .request_timeout(std::chrono::seconds{30})
        .log_level("debug")
        .add_route(fut::method::GET, "/health", &health)   // in-process route, no DLL
        .add_middleware(&log_request)                      // platform-wide, sees every request
        .add_module("cache", &cache)                       // extra service for app DLLs
        .run();                       // blocks until SIGINT/SIGTERM
}
```

Logging is part of the server too. Every logging parameter — patterns, file, sizes, levels — is
caller data; the server has none of its own:

```cpp
fut::exec::server()
    .log_to_console(true, fut::log::level::debug, "[{L}] {m}")   // pattern in, not baked
    .run();

fut::exec::server()
    .clear_log_chains()
    .add_log_chain(fut::log::chain_options{
        .minimum_level = fut::log::level::debug,
        .formatter = fut::log::json_formatter{},
        .sink = fut::log::file_sink{.path = "logs/json.log"},
    })
    .run();

fut::exec::server().log_to_file("logs/fut.log", 2 << 20, 3,
                             fut::log::level::trace, "{T} {g}: {m}");
```

Precedence: an explicit builder policy > `config.json` > the compiled-in default set
(`fut::exec::server::default_log_chains`, which itself takes the patterns and file settings as
arguments — `fut::log::console_pattern` / `fut::log::file_pattern` are only the stock values).

The config file describes the same thing as a **list of chains**, so adding a second file or a JSON
stream is an edit, not a rebuild:

```json
"log": {
  "chains": [
    { "sink": "console", "level": "info", "formatter": "pattern",
      "pattern": "{T} [{L:<8}] {g:<12} {m}", "color": true },
    { "sink": "file", "level": "trace", "formatter": "json",
      "path": "logs/fut.jsonl", "max_bytes": 2097152, "max_files": 3 }
  ]
}
```

| Field | Values | Meaning |
| --- | --- | --- |
| `level` | trace/debug/info/warning/error/critical/off | minimum level for this chain |
| `formatter` | `pattern` \| `json` | `pattern` uses `pattern`, `json` writes one object per line |
| `pattern` | format string | `{m}` message, `{t}/{T}` time, `{l}/{L}` level, `{g}` group, `{f}/{b}/{s}` file, `{u}` function, `{h}` thread |
| `sink` | `console` \| `file` | destination |
| `color` | bool | console only: ANSI colours |
| `path` | string | file only: sink path (parent directories are created) |
| `max_bytes` | number | file only: rotate above this size, 0 disables rotation |
| `max_files` | number | file only: backups kept as `<path>.1` … `<path>.<n-1>` |

An absent `log` section (or absent `chains`) means the stock set; an explicit `"chains": []`
means no logging at all. Every field is validated: an unknown formatter, sink or level — or a file
sink without a path — fails startup with a message naming the chain and the allowed values:

```
fatal: log.chains[0]: unknown formatter "xml" (expected "pattern" or "json")
```

Chains start when `run()` begins; one that cannot start is reported on stderr and skipped, so a
broken log path never takes the server down. If the config file itself fails to parse, the
default chains are installed before the fatal error is logged, so the message is not lost.

The builder also registers a built-in `GET /api/core/status` route (service version, ABI, loaded
apps and their route counts) and sets up the logging chains described below.

## Logging

Asynchronous, chain-based logging (`fut/log.hpp`, ported from HeliosView2): a call formats on the
calling thread and queues a single shared record to every chain; each chain owns a worker thread,
so a slow sink never blocks the caller.

Chains are installed by `fut::exec::server` from its configuration (see *The server
builder*); `fut::log::add_chain` is the primitive it calls, available directly to embedders that
manage logging themselves:

```cpp
fut::log::add_chain(fut::log::chain_options{
    .minimum_level = fut::log::level::info,
    .formatter = fut::log::pattern_formatter{"{T} [{L:<8}] {g:<12} {m}"},
    .sink = fut::log::console_sink{.color = true},
});
constexpr auto loader_log = fut::log::grp("loader");
loader_log.info("app '{}' ({} route(s)) <- {}", name, count, path);
```

* Levels: `trace/debug/info/warning/error/critical/off`; names are read from the enum declaration
  through std::meta reflection (`to_string` / `from_string`), so no name table exists to keep in
  sync. The configured level string is parsed the same way — an unknown name logs a warning and
  falls back to `info`.
* Pattern fields: `{m}` message, `{t}/{T}` (date)time, `{l}/{L}` level, `{g}` group,
  `{f}/{b}/{s}` file/base-file/location, `{u}` function, `{h}` thread; `json_formatter` emits
  one object per line. File paths print relative to the project root (`FUT_PROJECT_ROOT` from CMake).
* Sinks: colour console, rotating file (size + backup count), or an external `fut::log::sink`.
* Call `fut::log::shutdown()` before main returns (the server does); worker threads are never
  joined during static destruction.
* Configure with `"log_level": "debug"` in config.json or `--log-level debug`.

## Deploying a business update

```bash
cmake --build --preset release --target fut_app_demo
# copy build/release/bin/fut_app_demo.dll over the deployed one, restart the service
```

Neither the server executable nor the other app DLLs are rebuilt.

## ABI contract

* All modules must be built with the **same compiler and runtime** (the CMake presets guarantee it).
* A DLL exports `fut_app_register(fut_ctx*)` (required), `fut_abi_version()`,
  `fut_app_name()` and `fut_app_prefix()`. The server refuses DLLs whose ABI differs from its own.
* `fut_ctx` layout, `router_module` layout, `route_fn` and the module lookup signature are the
  binary contract: bump `fut::abi_version` (`fut/export.hpp`) when any of them changes incompatibly.
* Modules are keyed by the **mangled type name** (`typeid(T).name()`), compared as a string: the two
  sides are separate modules, so comparing `std::type_info` objects is not guaranteed. The router
  registers itself under `typeid(fut::http::router_module)`; apps reach it with the typed accessor.
* `request` string_views point into server-owned buffers and stay valid for the handler call only.
* `response` ownership moves to the server; exceptions escaping an action become 500 responses.
* A DLL must stay loaded while its routes are reachable — the route functions live in it.

## Reflection notes

* **Annotation routing**: `std::meta::info` is a structural type, so registration is a template
  parameterized on the entity's reflection; `annotations_of_with_type` filters the verbs,
  `parameters_of` drives binding, and `[:F:]` splices the call site.
* **Annotation values** are read through `std::meta::constant_of` (GCC 16.2 does not accept direct
  `[:annotation:]` splices yet).
* **cv-qualification**: the annotation *type* of a `constexpr` function's return value comes back
  cv-qualified — `type_of(ann) == ^^const http_action` — so verb detection filters on
  `^^const fut::http_action`. That single spot is the only place the quirk is visible.
* **Member enumeration** utilities for DTO introspection: `core/include/fut/reflect.hpp`
  (`member_count_v`, `member_names_v`, `for_each_member`); HTTP demo at `GET /api/demo/info`.
* **glaze** serializes aggregates via compile-time reflection — no field lists to maintain.

GCC 16.2 quirks the implementation works around:

1. splice-bearing branches must be `if constexpr` — GCC eagerly lowers splice expressions of all
   branches at instantiation, so an untaken branch would hit `constant_of(empty info)` and throw
   even though it never runs;
2. a splice used as a template argument must go through a `using` alias first;
3. enumeration is done with `template for (constexpr ... : std::define_static_array(...))` — the
   expansion statement makes the loop variable a constant expression, so it can be spliced and
   used as a template argument directly. A plain `for` loop variable cannot, which is why the old
   code needed fixed-size arrays plus index packs; a `std::array` copy is now only needed for the
   guarded `annotations_of` query (which cannot be a range in a throwing+`template for` context);
4. `annotations_of` throws on infos that represent no entity (an unresolved overload set); the
   scanner guards the query, since such an info can carry no annotation;
5. parameter names survive for `std::meta::identifier_of`, enabling the empty-key sugar.

## Project layout

One directory per target, and the library owns every header:

```
fut/
├── CMakeLists.txt / CMakePresets.json / vcpkg.json
├── cmake/fut.cmake          the add_app(name, prefix, sources...) helper
├── core/                    fut_core -- the library (shared: libfut_core.dll)
│   ├── CMakeLists.txt
│   ├── include/fut/         every public header
│   │   ├── ctx.hpp          fut_ctx: module lookup, the server <-> module contract
│   │   ├── scannable.hpp    "the scanner calls my operation()" (global)
│   │   ├── scan.hpp         the generic namespace scanner (global, not HTTP)
│   │   ├── app.hpp          generic app entry: scan + FUT_EXPORT_APP_NS
│   │   ├── annotations.hpp  generic annotation queries (fut::ann::of / first / has)
│   │   ├── inject.hpp       fut::inject: fill a namespace pointer from the ctx
│   │   ├── log.hpp          asynchronous logging (chains, sinks, levels)
│   │   ├── log_inject.hpp   fut::log_group: a group_ref wired to the server's log
│   │   ├── export.hpp       ABI constants + FUT_API (the core's export surface)
│   │   ├── reflect.hpp      std::meta member utilities
│   │   ├── json.hpp / url.hpp
│   │   ├── exec/            server, router, app_loader, config, module_registry
│   │   ├── http/            route, annotate, invoke, bind, form, filter, error
│   │   └── detail/          log format/path helpers, annotation text buffers
│   └── src/                 the implementation
│       ├── log.cpp          the log core
│       ├── router.cpp       registration, matching, middleware chains, dispatch
│       ├── app_loader.cpp   LoadLibrary + fut_app_register + module lookup
│       ├── config.cpp       config.json -> chains, with validation
│       ├── server.cpp       the fut::exec::server object (configure + run)
│       ├── listener.cpp     TCP accept/session loop
│       └── http_parser.* / http_message.*   the llhttp message layer (private)
├── exec/                    fut_server -- the thin shell
│   ├── CMakeLists.txt
│   └── main.cpp             argument parsing + server().run(); nothing else
├── apps/demo/               the reference app: one DLL, every feature, split into modules
└── tests/                   doctest suite (65 cases)
```

**The core is a real library.** `fut_core` is a shared library: the executable links it and
every app DLL links it too, so the log core, the router implementation and the module registry
exist exactly once in the process. `fut_server` is only `exec/main.cpp`; an app is only its
controllers and their definitions. The header-only parts (annotations, the scanner, the HTTP
types) are in `core/include` with everything else the library publishes.
## Configuration (config.json)

| Field | Default | Meaning |
| --- | --- | --- |
| host | 127.0.0.1 | listen address |
| port | 8080 | listen port |
| threads | 2 | io_context threads (including the main thread) |
| apps_dir | apps | app DLL directory; relative paths resolve against the executable |
| request_timeout_seconds | 30 | per-request read timeout |
| log_level | info | global level: trace/debug/info/warning/error/critical/off (matched through reflection) |
| log.chains | console(info) + file(trace) | list of log chains; see *The server builder* for the fields. Absent → stock set, `[]` → no logging |

## Tests

```bash
ctest --preset release   # 65 cases
```

* `test_router` — registration, app bookkeeping, full-path matching, literal-over-placeholder
  specificity, method filtering.
* `test_reflect_router` — the scan and controller registration: every binding style, string and DTO
  returns, controller path composition, nested namespaces, annotations stacked on one entity
  (including one that reads a sibling annotation's path), and that a controller nested inside a
  class is *not* scanned.
* `test_apps` — end-to-end against the real `fut_app_demo.dll`: the loader loads it, it registers
  through `fut_ctx`, and every feature is driven through `router::dispatch` exactly like the
  server does (all binding styles, return shapes, controller state, app middleware, injected
  services, error mapping).
* `test_config` — the config file as an interface: chain parsing, defaults, an explicit empty
  list, and validation errors for unknown formatters, sinks, levels and file settings.
* `test_http_parser` — the message layer: plain/chunked/bodies, split deliveries, pipelined
  requests, keep-alive rules (HTTP/1.0 vs 1.1, `Connection`), rejects garbage, header lookup.
* `test_form` — urlencoded and multipart parsing: decoding, quoted boundaries, empty file parts,
  malformed bodies reported with a reason, Content-Type dispatch.
* `test_router` also covers the middleware pipeline (platform chain wrapping matched routes and
  404s, short-circuiting, rewriting, app chains staying inside their app), the error mapping
  (own status, 404/405/400/500, custom `on_error` envelope, a middleware that throws) and module
  lookup (by type, ambiguous type `fut::module_error`, name-resolved ambiguity, wrong type,
  duplicate names refused).
* `test_log`, `test_reflect` — logging chains and std::meta utilities.
