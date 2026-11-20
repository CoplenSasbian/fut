# fut

C++26 coroutine web-server library: a small server executable plus **annotation-routed app DLLs**.
Business logic lives in one DLL per URL prefix; deploying an update means rebuilding that DLL and
swapping the file — the server and every other app stay untouched.

Actions are **free functions**; their C++26 annotations (P3394) are the routes and the parameter
bindings. Reflection is a **hard requirement**: the library is built on `std::meta` (P2996) and has
no reflection-free fallback path.

## Features

* Asio coroutines (`asio::awaitable`) + Boost.Beast HTTP server
* Annotation-declared routes on free functions: `[[=fut::http_post("/echo")]]`
* Automatic parameter binding: request body DTOs, query/path/header values, raw request escape hatch
* Return-type adaptation: `fut::response` passthrough, **plain strings served as text/plain**,
  glaze-serializable DTOs as JSON, anything else is a **compile error**
* One DLL per app (Django-style); the DLL exports a compile-time route table
* Fluent `server_builder` for the host executable (port, threads, timeouts, ...)
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

## Writing an app

An app is a Django-style unit: a name, a URL prefix, and a set of annotated free functions
inside **the namespace named after the app**. No base classes, no action list, no export
boilerplate — the CMake-generated export unit scans that namespace with std::meta reflection
(**recursively**: nested namespaces are descended into, so actions can be grouped freely) and
exports whatever carries a route annotation.

```cpp
// apps/web/web_app.hpp -- the whole app
#include <fut/annotate.hpp>
#include <fut/json.hpp>
#include <fut/refl_router.hpp>

namespace asio = boost::asio;

struct calc_in { double a{}; double b{}; std::string op{}; };
struct calc_out { double result{}; std::string op{}; };
struct sum_out { long long a{}; long long b{}; long long sum{}; };

namespace web {                        // <- the add_app() name, verbatim

[[=fut::http_get("/ping")]]
inline asio::awaitable<std::string> ping() { co_return "pong"; }   // plain string -> text/plain

[[=fut::http_post("/calc")]]
inline asio::awaitable<calc_out> calc([[=fut::from_body{}]] calc_in in) {  // JSON body -> DTO
    co_return calc_out{in.a * in.b, in.op};
}

[[=fut::http_get("/sum/{a}/{b}")]]
inline asio::awaitable<sum_out> sum([[=fut::from_path("a")]] long long a,   // path placeholders
                                    [[=fut::from_path("b")]] long long b) {
    co_return sum_out{a, b, a + b};
}

// nested groups are found by the recursive scan
namespace beta {
[[=fut::http_get("/beta/note")]]
inline asio::awaitable<std::string> beta_note() { co_return "from namespace web::beta"; }
} // namespace beta

} // namespace web
```

Wire it up with the `add_app()` helper (defined in `cmake/fut.cmake`, included by the
top-level `CMakeLists.txt`):

```cmake
# apps/pay/CMakeLists.txt -- this one line is the whole build script.
# Sources may be as many files as the app needs; see "One translation unit" below.
add_app(pay /api/pay pay_routes.hpp pay_beta.hpp)
```

`add_app(<name> <prefix> [sources...])` creates the `fut_app_<name>` MODULE target, places
`fut_app_<name>.dll` into `bin/apps`, adds the include path and the standard link set
(`Boost::asio`, `glaze::glaze`), defines `FUT_APP_NAME` / `FUT_APP_PREFIX` as quoted literals,
and validates the prefix format at configure time (must start with '/', must not end with '/').

Code injection: the export unit (`fut_export_<name>.cpp` in the build tree) is **generated** by
CMake at configure time —

```cpp
// Generated by fut's add_app(); do not edit.
#include <fut/refl_router.hpp>
#include <pay_routes.hpp>
#include <pay_beta.hpp>
FUT_EXPORT_APP_NS(FUT_APP_NAME, FUT_APP_PREFIX, ^^pay)
```

— so the app name, the prefix and the scanned namespace all live in exactly one place (the
`add_app()` call), and the C++ side can never drift out of sync. `FUT_EXPORT_APP_NS` scans the
namespace with std::meta reflection — every function carrying
`[[=fut::http_get/post/put/delete]]` becomes a route, **recursively through nested namespaces**,
nothing is listed by hand. A namespace with no annotated actions fails to compile with a
static_assert.

### One scanner (why headers included elsewhere cannot duplicate anything)

Scanning code exists in exactly **one** place per app: the generated export unit. `members_of` is
an ordinary library call, not a compiler hook — including the app header from `a.cpp`, `b.cpp`
or any other target puts no scanning code there, so nothing is scanned and nothing is registered
twice. Duplicates are structurally impossible, not merely discouraged.

Two app layouts are supported, picked per source extension:

**Declaration/definition split (recommended for real apps).** Annotated *declarations* in the
header, plain definitions in `.cpp` files — reflection works on declarations, no definition has
to be visible to the scanner; the generated unit's calls resolve through the linker.

**Annotation placement is enforced.** The scan sees exactly what the listed headers declare, so
route annotations have exactly one legal home:

| Where the annotation sits | Result |
| --- | --- |
| on a declaration in a listed header | scanned — the canonical place |
| only in a `.cpp` (on a definition) | **configure fails** (`add_app` greps listed `.cpp` files — a scan-blind annotation would otherwise be a silently missing route) |

```cpp
// web_routes.hpp -- annotated declarations; the generated unit scans these
namespace web {
[[=fut::http_post("/calc")]]
asio::awaitable<calc_out> calc([[=fut::from_body{}]] calc_in in);
}
```
```cpp
// web_routes.cpp -- an ordinary translation unit; no annotations here
#include "web_routes.hpp"
namespace web {
asio::awaitable<calc_out> calc(calc_in in) { co_return calc_out{in.a * in.b, in.op}; }
}
```
```cmake
add_app(web /api/web web_routes.hpp web_routes.cpp)
```

Headers are unity-included into the generated unit (several headers → all scanned); `.cpp`
sources are compiled normally and linked into the same DLL. `apps/web` demonstrates this split,
including a nested `web::beta` group in a second scanned region.

**Header-only (fine for small apps).** List only headers and define the actions `inline` there
(`apps/admin` does this); the generated unit includes them — one scan, one definition. For
headers shared with *other* targets, the ordinary header rule applies: defined entities must be
`inline`, which is exactly what header-only actions are.

Then reference the directory once from the top level (`add_subdirectory(apps/pay)`), build, and
the server picks the DLL up by prefix on the next start. (Hand-rolled export lists remain
available through `FUT_EXPORT_APP(name, prefix, ^^fn, ...)` for apps that prefer to be explicit.)

### Controllers

When actions need **state** — a pool, a cache, a counter — or a shared route prefix, group them in a
controller instead of free functions. A controller is a class annotated in the standard class-head
position (AFTER the `struct` keyword); the scan finds it in the same namespace as free actions and
contributes its member routes to the same table, with the class path joined onto every method path.
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

Rules: the annotation sits after the `struct` keyword (GCC requirement for class-head attributes);
member actions must be public (the invoke splices through them); parameter binding and return-type
adaptation are identical to free actions; free functions, nested namespaces and controllers can be
mixed freely in one app namespace — the scan walks all of them into one table.

## Parameter binding

| Parameter annotation | Source |
| --- | --- |
| `[[=fut::from_body{}]]` | request body JSON, parsed into the parameter type (glaze) |
| `[[=fut::from_query("k")]]` / `[[=fut::from_query{}]]` | query parameter (empty key -> parameter name) |
| `[[=fut::from_path("a")]]` / `[[=fut::from_path{}]]` | `{placeholder}` segment of the path pattern |
| `[[=fut::from_header("X-K")]]` | request header |
| none, type `const fut::request&` | the raw request (escape hatch) |
| none, anything else | **compile error** (static_assert explains) |

Binding failures (invalid JSON, missing value) become **400** responses; unmatched routes become **404**.
Scalars are parsed by glaze; `std::string` parameters are URL-decoded as-is.

### Return types

An action returns `asio::awaitable<T>` where `T` may be:

| `T` | Behavior |
| --- | --- |
| `fut::response` | passed through unchanged |
| `std::string` | served directly as the response body (`text/plain`) |
| glaze-serializable DTO | serialized to JSON automatically |
| anything else | **compile error** (static_assert explains) |

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
        .run();                       // blocks until SIGINT/SIGTERM
}
```

The builder also registers a built-in `/api/core/status` table (service version, ABI, loaded apps)
and sets up the logging chains described below.

## Logging

Asynchronous, chain-based logging (`fut/log.hpp`, ported from HeliosView2): a call formats on the
calling thread and queues a single shared record to every chain; each chain owns a worker thread,
so a slow sink never blocks the caller.

```cpp
// server_builder sets these two chains up; embedders can add their own:
fut::log::add_chain(fut::log::chain_options{
    .minimum_level = fut::log::level::info,
    .formatter = fut::log::pattern_formatter{"{T} [{L:<8}] {g:<12} {m}"},
    .sink = fut::log::console_sink{.color = true},
});
fut::log::add_chain(fut::log::chain_options{
    .minimum_level = fut::log::level::trace,
    .formatter = fut::log::pattern_formatter{"{T} [{L:<8}] {g:<12} {s}: {m}"},
    .sink = fut::log::file_sink{.path = "logs/fut.log", .max_bytes = 2 << 20, .max_files = 3},
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
# copy build/release/bin/apps/fut_app_admin.dll over the deployed one, restart the service
```

Neither the server executable nor the other app DLLs are rebuilt.

## ABI contract (`include/fut/`)

* All modules must be built with the **same compiler and runtime** (the CMake presets guarantee it).
* A DLL exports `fut_abi_version()` and `fut_app_table()` (see `FUT_EXPORT_APP`); the server
  refuses to load DLLs whose ABI version differs from its own.
* `request` string_views point into server-owned buffers and stay valid for the handler call only.
* `response` ownership moves to the server; exceptions escaping an action become 500 responses.
* Bump `fut::abi_version` whenever `fut/route.hpp` changes incompatibly.

## Reflection notes

* **Annotation routing**: `std::meta::info` is a structural type, so route generators are
  templates parameterized on the action's reflection; `annotations_of_with_type` filters the
  verbs, `parameters_of` drives binding, and `[:F:]` splices the call site.
* **Namespace scanning**: `members_of(^^ns, access_context)` also enumerates namespaces
  (GCC 16.2) — nested namespaces appear as `is_namespace` members, so the export unit's scan
  recurses and actions can be grouped in sub-namespaces without any list to maintain. Reflection
  works on **declarations**: annotated declarations in a header are scanned while their
  definitions compile as ordinary translation units.
* **Annotation values** are read through `std::meta::constant_of` (GCC 16.2 does not yet accept
  direct `[:annotation:]` splices).
* **Member enumeration** utilities for DTO introspection: `include/fut/reflect.hpp`
  (`member_count_v`, `member_names_v`, `for_each_member`); HTTP demo at `GET /api/admin/meta`.
* **glaze** serializes aggregates via compile-time reflection — no field lists to maintain.

GCC 16.2 quirks the implementation works around (see `fut/refl_router.hpp`):

1. splice-bearing branches must be `if constexpr` — GCC eagerly lowers splice expressions of all
   branches at instantiation, so an untaken branch would hit `constant_of(empty info)` and throw
   even though it never runs;
2. a splice used as a template argument must go through a `using` alias first;
3. type comparisons need `std::meta::dealias` (`^^std::string` reflects an alias);
4. consteval query vectors cannot be spliced by subscript (a library assert is not constexpr) —
   copy into `std::array` first;
5. parameter names survive for `std::meta::identifier_of`, enabling the empty-key sugar.

## Project layout

```
fut/
├── CMakeLists.txt / CMakePresets.json / vcpkg.json
├── cmake/fut.cmake         the add_app(name, prefix, sources...) helper
├── include/fut/            public library surface (shared by server and app DLLs)
│   ├── route.hpp           request/response, path matching, route_record, app_table, ABI version
│   ├── refl_router.hpp     annotation router + FUT_EXPORT_APP
│   ├── annotate.hpp        annotation types (verbs + parameter bindings)
│   ├── log.hpp             asynchronous logging (chains, sinks, reflection-based level names)
│   ├── reflect.hpp        std::meta member utilities
│   ├── json.hpp / url.hpp  glaze helpers / query parsing
│   └── detail/             log format/path helpers
├── src/exec/               server executable: builder, server, router, app_loader, config, log, main
├── apps/admin | apps/web   sample apps (free-function actions)
└── tests/                  doctest suite (20 cases)
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
ctest --preset release   # 20 cases: prefix routing, std::meta utilities, annotation router
                         # (compile-time table, body/query/path/request bindings, string and DTO
                         # returns, 400/404), logging (level names, file chain, level filter),
                         # end-to-end DLL loading
```

The end-to-end suite loads the real app DLLs through the loader and drives their exported tables.
