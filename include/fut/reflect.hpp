#pragma once
// fut/reflect.hpp -- C++26 static reflection utilities (std::meta / P2996).
//
// Reflection is a HARD REQUIREMENT of this library: there is no
// reflection-free fallback path. Compilers without std::meta fail to compile.
// GCC 15+ needs -freflection (added automatically by the top-level
// CMakeLists.txt, which also validates the toolchain up front).
//
// Usage:
//   fut::reflect::member_count_v<T>       number of non-static data members
//   fut::reflect::member_names_v<T>        std::array<std::string_view, N>
//   fut::reflect::for_each_member(obj, f) f(name, const member&) per member
//
// Note: std::meta query results (std::vector<meta::info>) only live within
// constant evaluation, so results are pinned down through consteval
// functions + constexpr variable templates.

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

// Reflection is a hard requirement: no reflection-free fallback.
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ < 15
#  error "fut requires C++26 std::meta reflection (GCC 15+ with -freflection)"
#endif
#include <meta>

namespace fut::reflect {

namespace meta = std::meta;

template <class T>
consteval std::size_t member_count() {
    return meta::nonstatic_data_members_of(^^T, meta::access_context::unchecked()).size();
}

template <class T, std::size_t I>
consteval meta::info member_info() {
    auto mems = meta::nonstatic_data_members_of(^^T, meta::access_context::unchecked());
    return mems[I];
}

template <class T>
inline constexpr std::size_t member_count_v = member_count<T>();

template <class T, std::size_t I>
inline constexpr meta::info member_info_v = member_info<T, I>();

template <class T, std::size_t I>
inline constexpr std::string_view member_name_v = meta::identifier_of(member_info<T, I>());

template <class T>
inline constexpr auto member_names_v = [] {
    std::array<std::string_view, member_count_v<T>> names{};
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((names[I] = member_name_v<T, I>), ...);
    }(std::make_index_sequence<member_count_v<T>>());
    return names;
}();

// Calls fn(std::string_view name, const member&) for every data member.
template <class T, class F>
constexpr void for_each_member(const T& value, F&& fn) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (fn(member_name_v<T, I>, value.[: member_info_v<T, I> :]), ...);
    }(std::make_index_sequence<member_count_v<T>>());
}

} // namespace fut::reflect
