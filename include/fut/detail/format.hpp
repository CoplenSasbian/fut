#pragma once

// A format string that remembers where it was written. std::format_string
// validates the pattern at compile time; bundling a std::source_location next
// to it lets a log call record its call site without a macro.

#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

namespace fut::detail {

template <typename... Args>
struct basic_location_fmt_string;

template <typename First, typename... Rest>
struct basic_location_fmt_string<First, Rest...> {
    template <class S>
    consteval basic_location_fmt_string(
            const S& fmt,
            std::source_location loc = std::source_location::current())
        : location(loc), m_fmt(fmt) {}

    std::string format(First&& first, Rest&&... rest) const {
        return std::vformat(m_fmt.get(), std::make_format_args(first, rest...));
    }

    std::source_location location;
    std::format_string<std::type_identity_t<First>, std::type_identity_t<Rest>...> m_fmt;
};

// No arguments: a string literal must stay convertible, so the text is kept as
// a string_view instead of a std::format_string<>.
template <>
struct basic_location_fmt_string<> {
    template <class S>
    consteval basic_location_fmt_string(
            const S& text,
            std::source_location loc = std::source_location::current())
        : location(loc), m_text(text) {}

    std::string format() const { return std::string(m_text); }

    std::source_location location;
    std::string_view m_text;
};

template <typename... Args>
using location_fmt_string = basic_location_fmt_string<std::type_identity_t<Args>...>;

// "int ns::f(int)" -> "ns::f", so a call site stays readable. The exact text of
// std::source_location::function_name() is compiler specific: GCC prints
// "<return type> <qualified name>(<params>)", MSVC
// "class <return type> __cdecl <name>(...)". Dropping the parameter list and
// then the return type covers both, and the anonymous namespace marker GCC adds
// ("{anonymous}::f") is not worth showing.
[[nodiscard]] inline std::string_view short_function_name(std::string_view full) noexcept {
    const auto paren = full.find('(');
    const auto signature = full.substr(0, paren == std::string_view::npos ? full.size() : paren);
    const auto space = signature.find_last_of(' ');
    std::string_view name =
            space == std::string_view::npos ? signature : signature.substr(space + 1);

    for (const std::string_view marker : {"{anonymous}::", "(anonymous namespace)::"}) {
        while (name.starts_with(marker)) name.remove_prefix(marker.size());
    }
    return name;
}

}  // namespace fut::detail
