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

* Asio coroutines (`asio::awaitable`) + Boost.Beast HTTP server
* Annotation-declared routes: `[[=fut::http_get("/ping")]]` on controller methods
* Automatic parameter binding: request body DTOs, query/path/header values, raw request escape hatch
* Return-type adaptation: `fut::response` passthrough, **plain strings served as text/plain**,
  glaze-serializable DTOs as JSON, anything else is a **compile error**
* One DLL per app (Django-style); registration through a context, no exported tables
* Generic scanner: adding a new scannable annotation changes neither the scanner nor CMake
* Fluent `server_builder` for the host executable (port, threads, timeouts, extra routes, ...)
* Asynchronous logging: colour console + rotating file chains, reflection-based level names
* doctest suite with full end-to-end coverage (loads the real DLLs)

## Requirements (hard)

| Component | Requirement |
| --- | --- |
| Compiler | **GCC >= 15 with `-freflection`** (std::meta reflection; no fallback path) |
| Dependencies | vcpkg: `boost-beast`, `boost-asio`, `glaze`, `doctest` |
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
    ├── fut_app_admin.dll
    └── fut_app_web.dll
```

```bash
curl http://127.0.0.1:8080/api/core/status
curl http://127.0.0.1:8080/api/admin/ping                 # {"pong":true,"app":"admin"}
curl -X POST -d '{"message":"hi","count":4}' http://127.0.0.1:8080/api/admin/echo
curl http://127.0.0.1:8080/api/web/sum/6/7                # {"a":6,"b":7,"sum":13}
curl http://127.0.0.1:8080/api/admin/raw                  # plain text, text/plain
```

## How an app is loaded

```
server                                        app DLL
  LoadLibrary("fut_app_admin.dll")
  ctx.app_name/app_prefix set from the exports
  fut_app_register(ctx)  ───────────────────▶  scan::run<^^admin>(ctx)
                                                  │
  ctx.get_module("router")  ◀───────────────────┤  find [[=fut::controller]] classes
   -> void*, cast to router_module*              │  call annotation.operation<Class>(ctx)
                                                  ▼
  router.add_route(self, GET, "/api/admin/ping", &fn)
```

* The server owns the router; a DLL never links against the executable.
* `fut_ctx` is the whole contract between them: a module lookup plus a server-private `host`.
* `get_module()` returns `void*`; the caller casts it to the type agreed for that module name.
  Casting to any other type is undefined behaviour.
* `ctx.app_name` / `ctx.app_prefix` are the app's identity, filled by the server from the DLL's
  exports before registration -- which is how annotations compose absolute paths without the
  scanner ever carrying a prefix argument.
* The loader checks `fut_abi_version()` and skips mismatched DLLs; `fut_app_name()` /
  `fut_app_prefix()` are used for logging and `/api/core/status`.

## Writing an app

An app is a Django-style unit: a name, a URL prefix, and controller classes in the namespace named
after the app.

```cpp
// apps/web/web_routes.hpp -- annotated controller declarations
#include <fut/http/http.hpp>

namespace asio = boost::asio;

struct calc_in  { double a{}; double b{}; std::string op{}; };
struct calc_out { double result{}; std::string op{}; };
struct sum_out  { long long a{}; long long b{}; long long sum{}; };

namespace web {                        // <- the add_app() name, verbatim

struct [[=fut::controller{}]] web_controller {

    [[=fut::http_get("/ping")]]
    asio::awaitable<std::string> ping();                              // plain string -> text/plain

    [[=fut::http_post("/calc")]]
    asio::awaitable<calc_out> calc([[=fut::from_body{}]] calc_in in); // JSON body -> DTO

    [[=fut::http_get("/sum/{a}/{b}")]]
    asio::awaitable<sum_out> sum([[=fut::from_path("a")]] long long a,
                                 [[=fut::from_path("b")]] long long b);
};

namespace beta {
// nested namespace: the scan descends into it, so this controller is found
struct [[=fut::controller("/beta")]] beta_controller {
    [[=fut::http_get("/note")]]
    asio::awaitable<std::string> note();                              // -> /beta/note
};
} // namespace beta

} // namespace web
```

Definitions live in `web_routes.cpp` as an ordinary translation unit
(`asio::awaitable<sum_out> web_controller::sum(long long a, long long b) { ... }`) — no annotations
there.

Wire it up with the `add_app()` helper (defined in `cmake/fut.cmake`, included by the top-level
`CMakeLists.txt`):

```cmake
# apps/web/CMakeLists.txt -- this one line is the whole build script.
add_app(web /api/web web_routes.hpp web_routes.cpp)
```

`add_app(<name> <prefix> [sources...])` creates the `fut_app_<name>` MODULE target, places
`fut_app_<name>.dll` into `bin/apps`, adds the include path and the standard link set
(`Boost::asio`, `glaze::glaze`), defines `FUT_APP_NAME` / `FUT_APP_PREFIX` as quoted literals, and
validates the prefix at configure time (must start with `/`, must not end with `/`).

### The generated export unit

CMake writes one translation unit per app at configure time:

```cpp
// Generated by fut's add_app(); do not edit.
#include <fut/app.hpp>          // generic entry: scan contract + export macro

#include <web_routes.hpp>

FUT_EXPORT_APP_NS(FUT_APP_NAME, FUT_APP_PREFIX, ^^web)
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
    fut::scan::run<^^web>(ctx);
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

The call is made on the annotation object itself:

```cpp
constexpr auto self = [: std::meta::constant_of(annotation) :];
self.template operation<Entity>(ctx);        // self.* is the annotation's configuration
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

**Header-only (fine for small apps).** List only headers and define the actions inline there
(`apps/admin` does this); the generated unit includes them — one scan, one definition.

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
add_app(admin /api/admin ...)   ->  "/api/admin"     (compile-time macro)
[[=fut::controller("/users")]]  ->  "/users"         (class annotation)
[[=fut::http_get("/{id}/name")]]->  "/{id}/name"     (method annotation)
                                    => /api/admin/users/{id}/name
```

One instance per controller type is created lazily per DLL (a thread-safe magic static), so state
persists across requests.

```cpp
namespace admin {

struct [[=fut::controller("/users")]] users_controller {
    std::atomic<std::uint64_t> hits_{0};

    [[=fut::http_get("/hits")]]                        // -> GET /api/admin/users/hits
    asio::awaitable<hits_out> hits() {
        co_return hits_out{hits_.fetch_add(1, std::memory_order_relaxed)};
    }

    [[=fut::http_get("/{id}/name")]]                   // -> GET /api/admin/users/{id}/name
    asio::awaitable<user_name_out> name([[=fut::from_path("id")]] long long id) {
        co_return user_name_out{id, "user-" + std::to_string(id)};
    }
};

} // namespace admin
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

Derive `fut::scannable` and give it a member `operation`; nothing else changes:

```cpp
struct my_annotation : fut::scannable {
    int priority = 0;

    template <std::meta::info Entity>
    void operation(fut::fut_ctx* ctx) const {
        // `this` is the annotation value attached to Entity (priority, path, ...);
        // ctx gives the app identity and any server module.
    }
};

struct [[=fut::my_annotation{.priority = 5}]] some_class { ... };
```

The scanner needs no change, `add_app` needs no change, and the new annotation can live in any
module (including one that never includes the HTTP headers).

## Parameter binding

| Parameter annotation | Source |
| --- | --- |
| `[[=fut::from_body{}]]` | request body JSON, parsed into the parameter type (glaze) |
| `[[=fut::from_query("k")]]` / `[[=fut::from_query{}]]` | query parameter (empty key -> parameter name) |
| `[[=fut::from_path("a")]]` / `[[=fut::from_path{}]]` | `{placeholder}` segment of the path pattern |
| `[[=fut::from_header("X-K")]]` | request header |
| none, type `const fut::request&` | the raw request (escape hatch) |
| none, anything else | **compile error** (static_assert explains) |

Binding failures (invalid JSON, missing value) become **400** responses; unmatched routes become
**404**; an unknown HTTP method becomes **405**. Scalars are parsed by glaze.

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

## The server builder

The host executable is a thin shell over `fut::exec::server_builder`; embedders use it directly:

```cpp
int main() {
    return fut::exec::server_builder()
        .config_file("config.json")   // JSON config; explicit values below override it
        .host("0.0.0.0")
        .port(8080)
        .threads(4)
        .apps_dir("apps")             // relative paths resolve against the executable
        .request_timeout(std::chrono::seconds{30})
        .log_level("debug")
        .add_route(fut::method::GET, "/health", &health)   // in-process route, no DLL
        .run();                       // blocks until SIGINT/SIGTERM
}
```

Logging is part of the builder too — the default set (colour console at info plus a rotating
file at trace) can be replaced or extended:

```cpp
server_builder()
    .clear_log_chains()                       // drop the defaults
    .add_log_chain(fut::log::chain_options{   // any chain you want
        .minimum_level = fut::log::level::debug,
        .formatter = fut::log::json_formatter{},
        .sink = fut::log::file_sink{.path = "logs/json.log"},
    })
    .run();

server_builder().log_to_console(true);                    // stdout only
server_builder().log_to_file("logs/fut.log", 2 << 20, 3); // file only, rotating
```

Chains start when `run()` begins; one that cannot start is reported on stderr and skipped, so a
broken log path never takes the server down.

The builder also registers a built-in `GET /api/core/status` route (service version, ABI, loaded
apps and their route counts) and sets up the logging chains described below.

## Logging

Asynchronous, chain-based logging (`fut/log.hpp`, ported from HeliosView2): a call formats on the
calling thread and queues a single shared record to every chain; each chain owns a worker thread,
so a slow sink never blocks the caller.

Chains are installed by `fut::exec::server_builder` from its configuration (see *The server
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
* Call `fut::log::shutdown()` before main returns (the builder does); worker threads are never
  joined during static destruction.
* Configure with `"log_level": "debug"` in config.json or `--log-level debug`.

## Deploying a business update

```bash
cmake --build --preset release --target fut_app_admin
# copy build/release/bin/fut_app_admin.dll over the deployed one, restart the service
```

Neither the server executable nor the other app DLLs are rebuilt.

## ABI contract

* All modules must be built with the **same compiler and runtime** (the CMake presets guarantee it).
* A DLL exports `fut_app_register(fut_ctx*)` (required), `fut_abi_version()`,
  `fut_app_name()` and `fut_app_prefix()`. The server refuses DLLs whose ABI differs from its own.
* `fut_ctx` layout, `router_module` layout and `route_fn` are the binary contract: bump
  `fut::abi_version` (`fut/export.hpp`) when any of them changes incompatibly.
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
* **Member enumeration** utilities for DTO introspection: `include/fut/reflect.hpp`
  (`member_count_v`, `member_names_v`, `for_each_member`); HTTP demo at `GET /api/admin/meta`.
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

```
fut/
├── CMakeLists.txt / CMakePresets.json / vcpkg.json
├── cmake/fut.cmake         the add_app(name, prefix, sources...) helper
├── include/fut/
│   ├── ctx.hpp             fut_ctx: module lookup, the server <-> module contract
│   ├── scannable.hpp       "the scanner calls my operation()" (global)
│   ├── scan.hpp            the generic namespace scanner (global, not HTTP)
│   ├── app.hpp             generic app entry: scan + FUT_EXPORT_APP_NS
│   ├── export.hpp          DLL ABI constants (FUT_EXPORT, abi_version)
│   ├── http/
│   │   ├── http.hpp        one include for the HTTP stack
│   │   ├── route.hpp       request/response, method enum, path matching, join_path
│   │   ├── router.hpp      router_module: the interface a DLL registers through
│   │   ├── bind.hpp        from_body / from_query / from_path / from_header
│   │   ├── invoke.hpp      argument binding, invoke, return adaptation
│   │   └── annotate.hpp    http_action verbs + controller (the scannable annotation)
│   ├── log.hpp             asynchronous logging (chains, sinks, reflection-based level names)
│   ├── reflect.hpp         std::meta member utilities
│   ├── json.hpp / url.hpp  glaze helpers / query parsing
│   └── detail/             log format/path helpers
├── src/exec/               server executable: builder, server, router, app_loader, config, log, main
├── apps/admin | apps/web   sample apps (controller style)
└── tests/                  doctest suite (23 cases)
```

## Configuration (config.json)

| Field | Default | Meaning |
| --- | --- | --- |
| host | 127.0.0.1 | listen address |
| port | 8080 | listen port |
| threads | 2 | io_context threads (including the main thread) |
| apps_dir | apps | app DLL directory; relative paths resolve against the executable |
| request_timeout_seconds | 30 | per-request read timeout |
| log_level | info | trace/debug/info/warning/error/critical/off (matched through reflection) |

## Tests

```bash
ctest --preset release   # 23 cases
```

* `test_router` — registration, app bookkeeping, full-path matching, literal-over-placeholder
  specificity, method filtering.
* `test_reflect_router` — the scan and controller registration: every binding style, string and DTO
  returns, controller path composition, nested namespaces, and that a controller nested inside a
  class is *not* scanned.
* `test_apps` — end-to-end: the loader loads the real DLLs, they register through `fut_ctx`, and
  the routes are driven exactly like the server does.
* `test_log`, `test_reflect` — logging chains and std::meta utilities.
