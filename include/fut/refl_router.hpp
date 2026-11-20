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

