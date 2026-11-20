#pragma once
// fut/reflect.hpp —— C++26 静态反射（std::meta / P2996）工具。
//
// GCC 15+ 需要 -freflection 编译开关（根 CMakeLists 已为 GNU 自动添加）。
// 用法：
//   fut::reflect::member_count_v<T>            成员数量
//   fut::reflect::member_names_v<T>            std::array<std::string_view, N>
//   fut::reflect::for_each_member(obj, f)      f(name, const成员引用) 逐成员回调
//
// 注意：std::meta 的查询结果（std::vector<meta::info>）只能活在常量求值内，
// 所以这里统一用 consteval 函数 + constexpr 变量模板把结果固化下来。

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#  define FUT_HAS_REFLECTION 1
#  include <meta>
#else
#  define FUT_HAS_REFLECTION 0
#endif

#if FUT_HAS_REFLECTION

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

// 逐成员回调：fn(std::string_view name, const 成员引用)
template <class T, class F>
constexpr void for_each_member(const T& value, F&& fn) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (fn(member_name_v<T, I>, value.[: member_info_v<T, I> :]), ...);
    }(std::make_index_sequence<member_count_v<T>>());
}

} // namespace fut::reflect

#endif // FUT_HAS_REFLECTION
