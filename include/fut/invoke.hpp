#pragma once

// fut/invoke.hpp -- the invoke machinery shared by all annotations.
//
// Parameter binding (request body DTOs, query/path/header values, the raw
// request escape hatch), the free-function and controller-member invoke
// coroutines, and the return-type adaptation (response / string / DTO).
// Annotation types include this to implement their operation(); the
// scanner never needs it.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/asio/awaitable.hpp>

#include <meta>

#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ < 15
#  error "fut requires C++26 std::meta reflection (GCC 15+ with -freflection)"
#endif

#include <fut/bind.hpp>
#include <fut/json.hpp>
#include <fut/route.hpp>
#include <fut/url.hpp>

namespace fut::refl {

namespace meta = std::meta;

// parameter binding (invoke machinery vocabulary)
// ---------------------------------------------------------------------------

template <std::size_t N>
consteval std::array<meta::info, N> copy_anns(meta::info entity, meta::info type) {
    std::array<meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : meta::annotations_of_with_type(entity, type))
        if (i < N) out[i++] = a;
    return out;
}

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

template <meta::info F, std::size_t P, class Ann>
consteval std::string_view param_key() {
    constexpr meta::info p = meta::parameters_of(F).data()[P];
    constexpr auto anns = copy_anns<1>(p, ^^Ann);
    constexpr bool has_ann = anns[0] != meta::info{};
    if constexpr (!has_ann) {
        return meta::identifier_of(p);
    } else {
        constexpr std::string_view k = [: meta::constant_of(anns[0]) :].key.view();
        return k.empty() ? meta::identifier_of(p) : k;
    }
}

template <meta::info F, std::size_t P>
decltype(auto) make_arg(const ::fut::request& req, const ::fut::path_match& pm) {
    constexpr meta::info p = meta::parameters_of(F).data()[P];
    using param_type = [: meta::type_of(p) :];
    using Bare = std::remove_cvref_t<param_type>;

    if constexpr (std::is_same_v<Bare, ::fut::request>) {
        return static_cast<const ::fut::request&>(req);
    }
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_body).empty()) {
        Bare v{};
        if (bool(glz::read_json(v, req.body)))
            throw ::fut::bind_error("invalid JSON body");
        return v;
    }
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_query).empty()) {
        constexpr std::string_view key = param_key<F, P, ::fut::from_query>();
        for (const auto& [kk, vv] : ::fut::parse_query(req.query))
            if (kk == key) return convert_scalar<Bare>(vv, key);
        throw ::fut::bind_error("missing query parameter: " + std::string(key));
    }
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_path).empty()) {
        constexpr std::string_view key = param_key<F, P, ::fut::from_path>();
        const std::string_view seg = pm.segment(key);
        if (seg.empty())
            throw ::fut::bind_error("missing path parameter: " + std::string(key));
        return convert_scalar<Bare>(seg, key);
    }
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
    constexpr std::size_t n = meta::parameters_of(F).size();
    return invoke_impl<F>(req, pm, std::make_index_sequence<n>{});
}

template <class T>
T& controller_instance() {
    static T instance;
    return instance;
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
    using CtrlT = [: Owner :];
    constexpr std::size_t n = meta::parameters_of(M).size();
    return invoke_member_impl<CtrlT, M>(req, pm, std::make_index_sequence<n>{});
}

consteval std::string_view join_pattern(std::string_view base, std::string_view sub) {
    std::string joined{base};
    joined += sub;
    return {std::define_static_string(joined), joined.size()};
}

} // namespace fut::refl
