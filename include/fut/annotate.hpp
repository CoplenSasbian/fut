#pragma once
// fut/annotate.hpp —— C++26 注解（P3394）类型定义。
//
// 用法（写在业务 app 的成员函数上）：
//   [[=fut::http_get ("/ping")]]           成员函数 -> GET 路由
//   [[=fut::http_post("/echo")]]           成员函数 -> POST 路由
//   [[=fut::http_put ("/x")]]              PUT 路由
//   [[=fut::http_delete("/x")]]            DELETE 路由
//
// 路由路径里可用 {占位}："/sum/{a}/{b}"，配合参数注解 from_path。
//
// 参数绑定注解（写在路由函数的形参上）：
//   [[=fut::from_body{}]]        形参 <- 请求体 JSON（glaze 解析为形参类型）
//   [[=fut::from_query("name")]] 形参 <- query 参数；key 为空时用参数名：
//                                  [[=fut::from_query{}]] std::string name
//   [[=fut::from_path("a")]]     形参 <- 路径 {占位} 段；key 为空时用参数名
//   [[=fut::from_header("X")]]   形参 <- 请求头
//   无注解且形参为 const fut::request& -> 原始请求（逃生舱）
//
// 注解类型必须是 structural（定长字符缓冲，不走堆），GCC 16.2 已验证。

#include <cstddef>
#include <string_view>

namespace fut {

inline constexpr std::size_t annot_path_max = 128;  // 路由路径/键的容量
inline constexpr std::size_t annot_key_max = 64;

namespace detail {

// 定长字符串缓冲（structural：char 数组 + constexpr 构造）
template <std::size_t Max>
struct text_buf {
    char chars[Max]{};

    constexpr text_buf() = default;
    constexpr text_buf(std::string_view s) {
        for (std::size_t i = 0; i < s.size() && i + 1 < Max; ++i) chars[i] = s[i];
    }
    constexpr std::size_t size() const {
        std::size_t n = 0;
        while (n < Max && chars[n]) ++n;
        return n;
    }
    constexpr std::string_view view() const { return {chars, size()}; }
};

template <std::size_t Max>
struct verb_base {
    text_buf<Max> path;

    constexpr verb_base() = default;
    constexpr verb_base(std::string_view s) : path(s) {}
};

} // namespace detail

// ---- HTTP 方法注解 ----
struct http_get  : detail::verb_base<annot_path_max> {
    using verb_base::verb_base;
};
struct http_post : detail::verb_base<annot_path_max> {
    using verb_base::verb_base;
};
struct http_put  : detail::verb_base<annot_path_max> {
    using verb_base::verb_base;
};
struct http_delete : detail::verb_base<annot_path_max> {
    using verb_base::verb_base;
};

// ---- 参数绑定注解 ----
struct from_body {};

struct from_query {
    detail::text_buf<annot_key_max> key;
    constexpr from_query() = default;
    constexpr from_query(std::string_view k) : key(k) {}
};

struct from_path {
    detail::text_buf<annot_key_max> key;
    constexpr from_path() = default;
    constexpr from_path(std::string_view k) : key(k) {}
};

struct from_header {
    detail::text_buf<annot_key_max> key;
    constexpr from_header() = default;
    constexpr from_header(std::string_view k) : key(k) {}
};

} // namespace fut
