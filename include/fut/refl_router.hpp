#pragma once
// fut/refl_router.hpp -- annotation-driven routing over free functions ("actions").
//
// An action is a free function annotated with a verb: [[=fut::http_get(...)]],
// [[=fut::http_post(...)]], [[=fut::http_put(...)]], [[=fut::http_delete(...)]];
// its path pattern may capture segments, e.g. "/sum/{a}/{b}".
// Parameters are bound from their annotations:
//   [[=fut::from_body{}]]              -> request body JSON, parsed into the parameter type (glaze)
//   [[=fut::from_query("k")]]          -> query parameter; an empty key means "use the parameter name"
//   [[=fut::from_path("a")]]           -> a "{name}" segment of the path pattern
//   [[=fut::from_header("X-K")]]       -> request header
//   no annotation, type const fut::request& -> the raw request (escape hatch)
//
// Return type: asio::awaitable<T> where T may be
//   * fut::response                -> passed through unchanged
//   * std::string                  -> served directly as the response body (text/plain)
//   * a glaze-serializable DTO     -> serialized to JSON automatically
//   * anything else                -> compile-time error (static_assert with guidance)
//
// The DLL-visible table is produced by (usually via the FUT_EXPORT_APP macro):
//   constexpr auto t = fut::refl::make_app_table<^^ping, ^^echo>("demo", "/api/demo");
//
// Reflection is a hard requirement: this header #errors out on compilers
// without std::meta. Annotation constant values are read through
// std::meta::constant_of because GCC 16.2 does not yet accept direct
// [:annotation:] splices.
//
// GCC implementation notes baked into the code below:
//   * branches containing splices must be written as "if constexpr" -- GCC
//     eagerly lowers the splice expressions of ALL branches at instantiation,
//     so an untaken branch would hit constant_of(empty info) and throw even
//     though it never runs;
//   * a splice used as a template argument must go through a type alias first;
//   * consteval query results (std::vector<std::meta::info>) cannot be
//     subscripted for splicing directly (a library assert is not constexpr);
//     copy them into std::array first.

#include <array>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

#include <meta>

#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ < 15
#  error "fut requires C++26 std::meta reflection (GCC 15+ with -freflection)"
#endif

#include <fut/annotate.hpp>
#include <fut/json.hpp>
#include <fut/route.hpp>
#include <fut/url.hpp>

namespace fut::refl {

namespace meta = std::meta;

// ---------------------------------------------------------------------------
// compile-time route metadata (per function reflection F)
// ---------------------------------------------------------------------------

consteval bool is_route_function(meta::info f) {
    return meta::is_function(f) &&
           (!meta::annotations_of_with_type(f, ^^fut::http_get).empty() ||
            !meta::annotations_of_with_type(f, ^^fut::http_post).empty() ||
            !meta::annotations_of_with_type(f, ^^fut::http_put).empty() ||
            !meta::annotations_of_with_type(f, ^^fut::http_delete).empty());
}

// Copy annotation infos into a fixed array so they can be spliced safely.
template <std::size_t N>
consteval std::array<meta::info, N> copy_anns(meta::info entity, meta::info type) {
    std::array<meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : meta::annotations_of_with_type(entity, type))
        if (i < N) out[i++] = a;
    return out;
}

template <meta::info F>
consteval std::string_view route_method() {
    if (!meta::annotations_of_with_type(F, ^^fut::http_get).empty()) return "GET";
    if (!meta::annotations_of_with_type(F, ^^fut::http_post).empty()) return "POST";
    if (!meta::annotations_of_with_type(F, ^^fut::http_put).empty()) return "PUT";
    if (!meta::annotations_of_with_type(F, ^^fut::http_delete).empty()) return "DELETE";
    return {};
}

// NOTE: "if constexpr" is mandatory here, see the header comment.
template <meta::info F>
consteval std::string_view route_pattern() {
    if constexpr (!meta::annotations_of_with_type(F, ^^fut::http_get).empty()) {
        constexpr auto anns = copy_anns<1>(F, ^^fut::http_get);
        return [: meta::constant_of(anns[0]) :].path.view();
    }
    else if constexpr (!meta::annotations_of_with_type(F, ^^fut::http_post).empty()) {
        constexpr auto anns = copy_anns<1>(F, ^^fut::http_post);
        return [: meta::constant_of(anns[0]) :].path.view();
    }
    else if constexpr (!meta::annotations_of_with_type(F, ^^fut::http_put).empty()) {
        constexpr auto anns = copy_anns<1>(F, ^^fut::http_put);
        return [: meta::constant_of(anns[0]) :].path.view();
    }
    else {
        constexpr auto anns = copy_anns<1>(F, ^^fut::http_delete);
        return [: meta::constant_of(anns[0]) :].path.view();
    }
}

// ---------------------------------------------------------------------------
// compile-time parameter metadata
// ---------------------------------------------------------------------------

template <meta::info F>
consteval std::size_t param_count() {
    return meta::parameters_of(F).size();
}

template <meta::info F, std::size_t P>
consteval meta::info nth_param() {
    return meta::parameters_of(F).data()[P];
}

// Key of a binding annotation; an empty key falls back to the parameter name
// (GCC preserves parameter names for identifier_of).
// Callers guarantee the parameter actually carries annotation Ann.
template <meta::info F, std::size_t P, class Ann>
consteval std::string_view param_key() {
    constexpr meta::info p = nth_param<F, P>();
    constexpr auto anns = copy_anns<1>(p, ^^Ann);
    constexpr bool has_ann = anns[0] != meta::info{};
    if constexpr (!has_ann) {
        return meta::identifier_of(p);
    } else {
        constexpr std::string_view k = [: meta::constant_of(anns[0]) :].key.view();
        return k.empty() ? meta::identifier_of(p) : k;
    }
}

// ---------------------------------------------------------------------------
// runtime argument construction
// ---------------------------------------------------------------------------

// Scalar conversion: std::string is taken as-is, other types parsed by glaze.
template <class A>
A convert_scalar(std::string_view raw, std::string_view what) {
    if constexpr (std::is_same_v<A, std::string>) {
        return std::string(raw);
    } else {
        A v{};
        if (bool(glz::read_json(v, raw)))
            throw ::fut::bind_error("bad value for " + std::string(what));
        return v;
    }
}

template <meta::info F, std::size_t P>
decltype(auto) make_arg(const ::fut::request& req, const ::fut::path_match& pm) {
    constexpr meta::info p = nth_param<F, P>();
    // A splice used as a template argument must be aliased to a type first.
    using param_type = [: meta::type_of(p) :];
    using Bare = std::remove_cvref_t<param_type>;

    // escape hatch: const fut::request& receives the raw request
    if constexpr (std::is_same_v<Bare, ::fut::request>) {
        return static_cast<const ::fut::request&>(req);
    }
    // request body -> parameter type (glaze compile-time reflection)
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_body).empty()) {
        Bare v{};
        if (bool(glz::read_json(v, req.body)))
            throw ::fut::bind_error("invalid JSON body");
        return v;
    }
    // query parameter
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_query).empty()) {
        constexpr std::string_view key = param_key<F, P, ::fut::from_query>();
        for (const auto& [kk, vv] : ::fut::parse_query(req.query))
            if (kk == key) return convert_scalar<Bare>(vv, key);
        throw ::fut::bind_error("missing query parameter: " + std::string(key));
    }
    // path "{placeholder}" segment
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_path).empty()) {
        constexpr std::string_view key = param_key<F, P, ::fut::from_path>();
        const std::string_view seg = pm.segment(key);
        if (seg.empty())
            throw ::fut::bind_error("missing path parameter: " + std::string(key));
        return convert_scalar<Bare>(seg, key);
    }
    // request header
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_header).empty()) {
        constexpr std::string_view key = param_key<F, P, ::fut::from_header>();
        for (const auto& [hn, hv] : req.headers)
            if (hn == key) return convert_scalar<Bare>(hv, key);
        throw ::fut::bind_error("missing header: " + std::string(key));
    } else {
        static_assert(!sizeof(Bare),
                      "fut: this route parameter needs a binding annotation "
                      "(from_body / from_query / from_path / from_header) "
                      "or must be 'const fut::request&'");
    }
}

// ---------------------------------------------------------------------------
// invocation + return-type adaptation
// ---------------------------------------------------------------------------

template <class Raw> struct await_value;
template <class V, class E>
struct await_value<boost::asio::awaitable<V, E>> { using type = V; };
template <class Raw> using await_value_t = typename await_value<Raw>::type;
template <class Raw>
inline constexpr bool is_awaitable_v = requires { typename await_value<Raw>::type; };

template <meta::info F, std::size_t... P>
boost::asio::awaitable<::fut::response> invoke_impl(const ::fut::request& req,
                                                    const ::fut::path_match& pm,
                                                    std::index_sequence<P...>) {
    using Raw = [: meta::return_type_of(F) :];
    static_assert(is_awaitable_v<Raw>,
                  "fut: an action must return asio::awaitable<T>");
    using Val = await_value_t<Raw>;

    if constexpr (std::is_same_v<Val, ::fut::response>) {
        co_return co_await ([: F :])(make_arg<F, P>(req, pm)...);
    }
    else if constexpr (std::is_same_v<Val, std::string>) {
        // plain string -> response body as-is
        std::string body = co_await ([: F :])(make_arg<F, P>(req, pm)...);
        ::fut::response r;
        r.status = 200;
        r.content_type = "text/plain; charset=utf-8";
        r.body = std::move(body);
        co_return r;
    }
    else if constexpr (requires {
                           glz::write_json(std::declval<Val&>(), std::declval<std::string&>());
                       }) {
        // glaze-serializable DTO -> JSON
        co_return ::fut::json(co_await ([: F :])(make_arg<F, P>(req, pm)...));
    }
    else {
        static_assert(!sizeof(Val),
                      "fut: unsupported action return type -- expected "
                      "awaitable<fut::response>, awaitable<std::string>, or "
                      "awaitable<DTO> with a glaze-serializable DTO");
    }
}

template <meta::info F>
boost::asio::awaitable<::fut::response> invoke_route(const ::fut::request& req,
                                                     const ::fut::path_match& pm) {
    constexpr std::size_t n = param_count<F>();
    return invoke_impl<F>(req, pm, std::make_index_sequence<n>{});
}

// ---------------------------------------------------------------------------
// app table construction
// ---------------------------------------------------------------------------

template <meta::info... Fs>
constexpr std::array<::fut::route_record, sizeof...(Fs)> records_v = [] {
    std::array<::fut::route_record, sizeof...(Fs)> out{};
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        constexpr meta::info fns[]{Fs...};
        ((out[I] = ::fut::route_record{route_method<fns[I]>(),
                                        route_pattern<fns[I]>(),
                                        &invoke_route<fns[I]>}),
         ...);
    }(std::make_index_sequence<sizeof...(Fs)>());
    return out;
}();

// constexpr auto t = fut::refl::make_app_table<^^ping, ^^echo>("demo", "/api/demo");
template <meta::info... Fs>
constexpr ::fut::app_table make_app_table(std::string_view name, std::string_view prefix) {
    return ::fut::app_table{name, prefix, records_v<Fs...>.data(), sizeof...(Fs)};
}

// ---------------------------------------------------------------------------
// namespace-scanned tables
//
// Actions live in the namespace named after the app; every function carrying
// one of the verb annotations is discovered automatically -- and the scan is
// recursive, so nested namespaces are descended into and an app can group its
// actions freely. Annotated controller classes (see the controllers section
// below) in the same namespace contribute their member routes to the same
// table. GCC 16.2 supports members_of() over namespaces (including nested
// ones as members), which is what makes this work.
//
//     namespace admin {
//         [[=fut::http_get("/ping")]] asio::awaitable<pong_dto> ping() { ... }
//         namespace reports {
//             [[=fut::http_post("/echo")]] asio::awaitable<out> echo(...) { ... }
//         }
//     }
//     FUT_EXPORT_APP_NS("admin", "/api/admin", ^^admin)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// controllers
//
// A controller is a class annotated with [[=fut::controller("/base")]]
// (annotation AFTER the struct keyword, the standard class-head position).
// Its annotated member functions become routes whose pattern is the class
// path joined with the method path. One instance per controller type is
// constructed lazily on first use (a thread-safe magic static held by the
// DLL), so actions can carry state: pools, caches, counters.
//
//     struct [[=fut::controller("/users")]] users_controller {
//         std::atomic<std::uint64_t> hits_{0};
//         [[=fut::http_get("/hits")]]
//         asio::awaitable<hits_out> hits() { co_return hits_out{++hits_}; }
//     };
//
// Free actions and controllers can be mixed freely inside one app namespace.
// ---------------------------------------------------------------------------

consteval bool is_controller_type(meta::info m) {
    return meta::is_type(m) && meta::is_class_type(m) &&
           !meta::annotations_of_with_type(m, ^^fut::controller).empty();
}

consteval std::size_t ctrl_route_count(meta::info t) {
    std::size_t n = 0;
    for (auto m : meta::members_of(t, meta::access_context::unchecked()))
        if (meta::is_class_member(m) && is_route_function(m)) ++n;
    return n;
}

consteval meta::info ctrl_nth_route(meta::info t, std::size_t idx) {
    std::size_t seen = 0;
    for (auto m : meta::members_of(t, meta::access_context::unchecked())) {
        if (!meta::is_class_member(m) || !is_route_function(m)) continue;
        if (seen == idx) return m;
        ++seen;
    }
    return meta::info{};
}

// One instance per controller type, constructed on first use.
template <class T>
T& controller_instance() {
    static T instance;
    return instance;
}

// Pattern of a controller method: class path joined with the method path.
// define_static_string gives the joined text static storage duration.
template <meta::info Owner, meta::info Fn>
consteval std::string_view controller_pattern() {
    constexpr auto anns = copy_anns<1>(Owner, ^^fut::controller);
    constexpr std::string_view base = [: meta::constant_of(anns[0]) :].path.view();
    constexpr std::string_view sub = route_pattern<Fn>();
    std::string joined{base};
    joined += sub;
    return {std::define_static_string(joined), joined.size()};
}

template <class T, meta::info M, std::size_t... P>
boost::asio::awaitable<::fut::response> invoke_member_impl(
    const ::fut::request& req, const ::fut::path_match& pm, std::index_sequence<P...>) {
    using Raw = [: meta::return_type_of(M) :];
    static_assert(is_awaitable_v<Raw>,
                  "fut: a controller action must return asio::awaitable<T>");
    using Val = await_value_t<Raw>;

    auto& self = controller_instance<T>();
    if constexpr (std::is_same_v<Val, ::fut::response>) {
        co_return co_await (self.[: M :])(make_arg<M, P>(req, pm)...);
    }
    else if constexpr (std::is_same_v<Val, std::string>) {
        // plain string -> response body as-is
        std::string body = co_await (self.[: M :])(make_arg<M, P>(req, pm)...);
        ::fut::response r;
        r.status = 200;
        r.content_type = "text/plain; charset=utf-8";
        r.body = std::move(body);
        co_return r;
    }
    else if constexpr (requires {
                           glz::write_json(std::declval<Val&>(), std::declval<std::string&>());
                       }) {
        // glaze-serializable DTO -> JSON
        co_return ::fut::json(co_await (self.[: M :])(make_arg<M, P>(req, pm)...));
    }
    else {
        static_assert(!sizeof(Val),
                      "fut: unsupported controller action return type -- expected "
                      "awaitable<fut::response>, awaitable<std::string>, or "
                      "awaitable<DTO> with a glaze-serializable DTO");
    }
}

template <meta::info Owner, meta::info M>
boost::asio::awaitable<::fut::response> invoke_member_route(const ::fut::request& req,
                                                           const ::fut::path_match& pm) {
    // a splice used as a template argument must be aliased to a type first
    using CtrlT = [: Owner :];
    constexpr std::size_t n = param_count<M>();
    return invoke_member_impl<CtrlT, M>(req, pm, std::make_index_sequence<n>{});
}

// ---------------------------------------------------------------------------
// recursive namespace scan (free actions + controllers, one index space)
// ---------------------------------------------------------------------------

// A located route: fn is the function's reflection; owner is the controller
// type's reflection for member routes, null info for free functions.
struct route_location {
    meta::info fn{};
    meta::info owner{};
};

consteval std::size_t ns_scan_count(meta::info ns) {
    std::size_t n = 0;
    for (auto m : meta::members_of(ns, meta::access_context::unchecked())) {
        if (is_route_function(m)) ++n;
        else if (is_controller_type(m)) n += ctrl_route_count(m);
        else if (meta::is_namespace(m)) n += ns_scan_count(m);
    }
    return n;
}

consteval route_location ns_scan_locate(meta::info ns, std::size_t idx) {
    for (auto m : meta::members_of(ns, meta::access_context::unchecked())) {
        if (is_route_function(m)) {
            if (idx == 0) return route_location{m, meta::info{}};
            --idx;
        } else if (is_controller_type(m)) {
            const std::size_t sub = ctrl_route_count(m);
            if (idx < sub) return route_location{ctrl_nth_route(m, idx), m};
            idx -= sub;
        } else if (meta::is_namespace(m)) {
            const std::size_t sub = ns_scan_count(m);
            if (idx < sub) return ns_scan_locate(m, idx);
            idx -= sub;
        }
    }
    return {};
}

template <meta::info Ns>
consteval std::size_t ns_route_count() { return ns_scan_count(Ns); }

template <meta::info Ns>
consteval route_location ns_route_at(std::size_t idx) { return ns_scan_locate(Ns, idx); }

template <meta::info Ns, std::size_t I>
constexpr ::fut::route_record record_at() {
    constexpr route_location loc = ns_route_at<Ns>(I);
    // NOTE: "if constexpr" is mandatory -- GCC eagerly lowers splice
    // expressions of all branches at instantiation.
    if constexpr (loc.owner == meta::info{}) {
        return ::fut::route_record{
            route_method<loc.fn>(),
            route_pattern<loc.fn>(),
            &invoke_route<loc.fn>};
    } else {
        return ::fut::route_record{
            route_method<loc.fn>(),
            controller_pattern<loc.owner, loc.fn>(),
            &invoke_member_route<loc.owner, loc.fn>};
    }
}

template <meta::info Ns, std::size_t... I>
constexpr std::array<::fut::route_record, sizeof...(I)> ns_records_impl(
    std::index_sequence<I...>) {
    std::array<::fut::route_record, sizeof...(I)> out{};
    ((out[I] = record_at<Ns, I>()), ...);
    return out;
}

template <meta::info Ns>
constexpr auto ns_records_v =
    ns_records_impl<Ns>(std::make_index_sequence<ns_route_count<Ns>()>{});

// constexpr auto t = fut::refl::make_scanned_table<^^admin>("admin", "/api/admin");
template <meta::info Ns>
constexpr ::fut::app_table make_scanned_table(std::string_view name, std::string_view prefix) {
    static_assert(ns_route_count<Ns>() > 0,
                  "fut: no [[=fut::http_*]] annotated actions were found in the namespace");
    return ::fut::app_table{name, prefix, ns_records_v<Ns>.data(), ns_route_count<Ns>()};
}

} // namespace fut::refl

// ---------------------------------------------------------------------------
// DLL export helper -- place at file scope, once per DLL:
//     FUT_EXPORT_APP("admin", "/api/admin", ^^ping, ^^echo)
// exports fut_abi_version() and fut_app_table() for the server loader.
// ---------------------------------------------------------------------------

#define FUT_EXPORT_APP(NAME, PREFIX, ...)                                         \
    namespace {                                                                    \
    constexpr auto fut_app_table_v =                                               \
        ::fut::refl::make_app_table<__VA_ARGS__>((NAME), (PREFIX));                 \
    }                                                                              \
    extern "C" {                                                                    \
    FUT_EXPORT std::uint32_t fut_abi_version() { return ::fut::abi_version; }      \
    FUT_EXPORT const ::fut::app_table* fut_app_table() {                          \
        return &fut_app_table_v;                                                    \
    }                                                                              \
    }

// ---------------------------------------------------------------------------
// DLL export helper without any action list: NS is the reflection of the
// namespace holding the actions -- conventionally the namespace named after
// the app, e.g. ^^admin; the scan descends into nested namespaces. Every
// annotated function found is exported. This is what add_app()'s generated
// unit uses.
//     FUT_EXPORT_APP_NS("admin", "/api/admin", ^^admin)
// ---------------------------------------------------------------------------

#define FUT_EXPORT_APP_NS(NAME, PREFIX, NS)                                        \
    namespace {                                                                    \
    constexpr auto fut_app_table_v =                                               \
        ::fut::refl::make_scanned_table<(NS)>((NAME), (PREFIX));                   \
    }                                                                              \
    extern "C" {                                                                    \
    FUT_EXPORT std::uint32_t fut_abi_version() { return ::fut::abi_version; }      \
    FUT_EXPORT const ::fut::app_table* fut_app_table() {                          \
        return &fut_app_table_v;                                                    \
    }                                                                              \
    }

