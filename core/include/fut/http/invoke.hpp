#pragma once

// fut/http/invoke.hpp -- the invoke machinery shared by all annotations.
//
// Parameter binding (request body DTOs, query/path/header values, the raw
// request escape hatch), the free-function and controller-member invoke
// coroutines, and the return-type adaptation (response / string / DTO).
// Annotation types include this to implement their operation(); the
// scanner never needs it.

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/asio/awaitable.hpp>

#include <meta>

#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ < 15
#  error "fut requires C++26 std::meta reflection (GCC 15+ with -freflection)"
#endif

#include <fut/http/bind.hpp>
#include <fut/http/form.hpp>
#include <fut/json.hpp>
#include <fut/http/route.hpp>
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
    }
    else if constexpr (std::is_same_v<A, bool>) {
        // HTML forms send checkboxes as "on"; accept the obvious spellings.
        if (raw == "on" || raw == "1" || raw == "true") return true;
        if (raw == "off" || raw == "0" || raw == "false") return false;
        throw ::fut::bind_error("bad boolean for " + std::string(what) + ": '" +
                                std::string(raw) + "'");
    }
    else {
        A v{};
        if (bool(glz::read_json(v, raw)))
            throw ::fut::bind_error("bad value for " + std::string(what));
        return v;
    }
}

// A parameter bound from a set of named values can either be a single value
// (std::string / scalar) or a whole aggregate whose members are filled by
// name. This decides which.
template <class T>
constexpr bool is_field_set_dto_v =
    std::is_class_v<T> && !std::is_same_v<T, std::string> &&
    !std::is_same_v<T, std::string_view> && !std::is_same_v<T, ::fut::uploaded_file> &&
    requires(T& value, std::string& out) { glz::write_json(value, out); };

// Fills an aggregate from name/value pairs: every member present in the set is
// assigned; members that are absent keep the value they were default-built
// with. Unknown names are ignored, like extra JSON keys.
template <class Dto, class Pairs>
Dto bind_field_set(const Pairs& pairs) {
    Dto dto{};
    template for (constexpr meta::info member : std::define_static_array(
                      meta::nonstatic_data_members_of(
                          ^^Dto, meta::access_context::unchecked()))) {
        constexpr std::string_view name = meta::identifier_of(member);
        using Member = [: meta::type_of(member) :];
        for (const auto& [key, value] : pairs) {
            if (key != name) continue;
            if constexpr (std::is_class_v<std::remove_cvref_t<Member>> &&
                          !std::is_same_v<std::remove_cvref_t<Member>, std::string>) {
                static_assert(!sizeof(Member),
                              "fut: a form/query DTO member must be a scalar or a string; "
                              "nested aggregates are not bound");
            } else {
                dto.[: member :] = convert_scalar<std::remove_cvref_t<Member>>(value, name);
            }
            break;
        }
    }
    return dto;
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

// True when any parameter asks for a form field or an uploaded file; the
// caller then parses the body once instead of once per parameter.
template <meta::info F>
consteval bool needs_form() {
    // Iterate the parameter list directly: a constexpr std::vector variable
    // cannot be formed in constant evaluation here (its allocation would
    // escape the expression).
    try {
        for (auto p : meta::parameters_of(F)) {
            if (!meta::annotations_of_with_type(p, ^^::fut::from_form).empty() ||
                !meta::annotations_of_with_type(p, ^^::fut::from_file).empty())
                return true;
        }
    } catch (...) {
    }
    return false;
}

template <meta::info F, std::size_t P>
decltype(auto) make_arg(const ::fut::request& req, const ::fut::path_match& pm,
                        ::fut::response& out, const ::fut::form_data* form) {
    constexpr meta::info p = meta::parameters_of(F).data()[P];
    using param_type = [: meta::type_of(p) :];
    using Bare = std::remove_cvref_t<param_type>;

    if constexpr (std::is_same_v<Bare, ::fut::request>) {
        return static_cast<const ::fut::request&>(req);
    }
    else if constexpr (std::is_same_v<Bare, ::fut::response>) {
        return static_cast<::fut::response&>(out);
    }
    else if constexpr (std::is_same_v<Bare, ::fut::path_match>) {
        return static_cast<const ::fut::path_match&>(pm);
    }
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_body).empty()) {
        if (!::fut::utf8_valid(req.body))
            throw ::fut::bind_error("request body is not valid UTF-8");
        Bare v{};
        if (bool(glz::read_json(v, req.body)))
            throw ::fut::bind_error("invalid JSON body");
        return v;
    }
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_query).empty()) {
        if constexpr (is_field_set_dto_v<Bare>) {
            return bind_field_set<Bare>(::fut::parse_query(req.query));
        } else {
            constexpr std::string_view key = param_key<F, P, ::fut::from_query>();
            for (const auto& [kk, vv] : ::fut::parse_query(req.query))
                if (kk == key) return convert_scalar<Bare>(vv, key);
            throw ::fut::bind_error("missing query parameter: " + std::string(key));
        }
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
    }
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_form).empty()) {
        if (!form) throw ::fut::bind_error("no form body on this request");
        if constexpr (is_field_set_dto_v<Bare>) {
            return bind_field_set<Bare>(form->fields);
        } else {
            constexpr std::string_view key = param_key<F, P, ::fut::from_form>();
            if (const std::string_view* value = form->field(key))
                return convert_scalar<Bare>(*value, key);
            throw ::fut::bind_error("missing form field: " + std::string(key));
        }
    }
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_file).empty()) {
        constexpr std::string_view key = param_key<F, P, ::fut::from_file>();
        static_assert(std::is_same_v<Bare, ::fut::uploaded_file>,
                      "fut: a from_file parameter must be fut::uploaded_file");
        if (!form)
            throw ::fut::bind_error("no multipart body on this request");
        if (const ::fut::uploaded_file* file = form->file(key)) return *file;
        throw ::fut::bind_error("missing uploaded file: " + std::string(key));
    } else {
        static_assert(!sizeof(Bare),
                      "fut: this route parameter needs a binding annotation "
                      "(from_body / from_query / from_path / from_header / "
                      "from_form / from_file) or must be 'const fut::request&' / "
                      "'const fut::path_match&' / 'fut::response&'");
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

// What an action returned becomes the body of the connection's response.
template <class T>
void adapt_into(::fut::response& out, T&& value) {
    using V = std::remove_cvref_t<T>;
    static_assert(!std::is_same_v<V, ::fut::response>,
                  "an action returns a payload (a DTO, a string) or writes into "
                  "fut::response& -- it never returns a response object");
    if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view> ||
                  std::is_same_v<V, const char*>) {
        out.set_text(std::string(std::forward<T>(value)));
    } else if constexpr (std::is_arithmetic_v<V>) {
        out.set_text(std::to_string(value));
    } else {
        out.dto(value);
    }
}

template <meta::info F, std::size_t... P>
boost::asio::awaitable<void> invoke_free_impl(
    const ::fut::request& req, const ::fut::path_match& pm, ::fut::response& out,
    std::index_sequence<P...>) {
    using Raw = [: meta::return_type_of(F) :];
    static_assert(is_awaitable_v<Raw>,
                  "fut: an action must return boost::asio::awaitable<...>");
    using Val = await_value_t<Raw>;

    constexpr bool wants_form = needs_form<F>();
    std::optional<::fut::form_data> parsed_form{};
    if constexpr (wants_form) parsed_form = ::fut::parse_form(req);
    const ::fut::form_data* form = nullptr;
    if constexpr (wants_form) form = &*parsed_form;

    if constexpr (std::is_void_v<Val>) {
        co_await ([: F :])(make_arg<F, P>(req, pm, out, form)...);
    } else {
        auto value = co_await ([: F :])(make_arg<F, P>(req, pm, out, form)...);
        adapt_into(out, std::move(value));
    }
}

template <meta::info F>
boost::asio::awaitable<void> invoke_route(const ::fut::request& req,
                                          const ::fut::path_match& pm,
                                          ::fut::response& out) {
    constexpr std::size_t n = meta::parameters_of(F).size();
    return invoke_free_impl<F>(req, pm, out, std::make_index_sequence<n>{});
}

template <class T>
T& controller_instance() {
    static T instance;
    return instance;
}

template <class T, meta::info M, std::size_t... P>
boost::asio::awaitable<void> invoke_member_impl(
    const ::fut::request& req, const ::fut::path_match& pm, ::fut::response& out,
    std::index_sequence<P...>) {
    using Raw = [: meta::return_type_of(M) :];
    static_assert(is_awaitable_v<Raw>,
                  "fut: an action must return boost::asio::awaitable<...>");
    using Val = await_value_t<Raw>;

    constexpr bool wants_form = needs_form<M>();
    std::optional<::fut::form_data> parsed_form{};
    if constexpr (wants_form) parsed_form = ::fut::parse_form(req);
    const ::fut::form_data* form = nullptr;
    if constexpr (wants_form) form = &*parsed_form;

    auto& self = controller_instance<T>();
    if constexpr (std::is_void_v<Val>) {
        co_await (self.[: M :])(make_arg<M, P>(req, pm, out, form)...);
    } else {
        auto value = co_await (self.[: M :])(make_arg<M, P>(req, pm, out, form)...);
        adapt_into(out, std::move(value));
    }
}

template <meta::info Owner, meta::info M>
boost::asio::awaitable<void> invoke_member_route(const ::fut::request& req,
                                                 const ::fut::path_match& pm,
                                                 ::fut::response& out) {
    using CtrlT = [: Owner :];
    constexpr std::size_t n = meta::parameters_of(M).size();
    return invoke_member_impl<CtrlT, M>(req, pm, out, std::make_index_sequence<n>{});
}

} // namespace fut::refl
