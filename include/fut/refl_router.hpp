#pragma once
// fut/refl_router.hpp —— 注解驱动的路由分发（C++26 std::meta + P3394 注解）。
//
// app 的 handle() 只需一行：
//     co_return co_await fut::refl::dispatch(*this, req);
//
// dispatch 在编译期扫描 T 的成员函数，收集带 [[=fut::http_get/post/put/delete]]
// 注解的函数生成静态路由表（方法+路径模式+调用器），运行时按 (method, path)
// 匹配；调用器依据每个形参上的 [[=fut::from_body/from_query/from_path/from_header]]
// 注解自动构造实参，并按返回值类型（response / 任意 DTO）自动包装。
//
// 编译器要求：GCC 15+（-freflection）。注解常量值通过 std::meta::constant_of
// 访问（GCC 16.2 尚未开放注解的 [:ann:] 直接拼接，constant_of 为等价入口）。
// 注：路由方法需为 public（拼接处做访问检查）。

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

#include <fut/annotate.hpp>
#include <fut/app.hpp>
#include <fut/json.hpp>
#include <fut/url.hpp>

// 反射为硬性要求：不支持 std::meta 的编译器直接编译失败
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ < 15
#  error "fut requires C++26 std::meta reflection (GCC 15+ with -freflection)"
#endif
#include <meta>

namespace fut {

// 参数绑定失败（JSON 解析失败、query/path/header 缺失等）-> dispatch 转 400
struct bind_error : std::runtime_error {
    explicit bind_error(std::string_view what) : std::runtime_error(std::string(what)) {}
};

namespace refl {

namespace meta = std::meta;

// ---------------------------------------------------------------------------
// 路径模式匹配：literal 段相等，{name} 段捕获
// ---------------------------------------------------------------------------

struct path_match {
    static constexpr std::size_t max_named = 8;
    std::array<std::pair<std::string_view, std::string_view>, max_named> named{};
    std::size_t named_count = 0;

    [[nodiscard]] std::string_view segment(std::string_view key) const {
        for (std::size_t i = 0; i < named_count; ++i)
            if (named[i].first == key) return named[i].second;
        return {};
    }
};

inline bool match_pattern(std::string_view pattern, std::string_view path, path_match& out) {
    auto next_seg = [](std::string_view s, std::size_t& i) {
        while (i < s.size() && s[i] == '/') ++i;
        const std::size_t b = i;
        while (i < s.size() && s[i] != '/') ++i;
        return s.substr(b, i - b);
    };
    std::string_view pat[16];
    std::string_view seg[16];
    std::size_t np = 0, ns = 0, i = 0;
    for (std::string_view s = next_seg(pattern, i); !s.empty() && np < 16;
         s = next_seg(pattern, i))
        pat[np++] = s;
    i = 0;
    for (std::string_view s = next_seg(path, i); !s.empty() && ns < 16;
         s = next_seg(path, i))
        seg[ns++] = s;
    if (np != ns) return false;

    for (std::size_t k = 0; k < np; ++k) {
        const std::string_view p = pat[k];
        if (p.size() >= 2 && p.front() == '{' && p.back() == '}') {
            if (out.named_count >= path_match::max_named) return false;
            out.named[out.named_count++] = {p.substr(1, p.size() - 2), seg[k]};
        } else if (p != seg[k]) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 编译期：枚举注解路由函数、提取方法与路径模式
// ---------------------------------------------------------------------------

consteval bool is_route_member(meta::info m) {
    return meta::is_class_member(m) && meta::is_function(m) &&
           (!meta::annotations_of_with_type(m, ^^fut::http_get).empty() ||
            !meta::annotations_of_with_type(m, ^^fut::http_post).empty() ||
            !meta::annotations_of_with_type(m, ^^fut::http_put).empty() ||
            !meta::annotations_of_with_type(m, ^^fut::http_delete).empty());
}

template <class T>
consteval std::size_t route_count() {
    std::size_t n = 0;
    for (auto m : meta::members_of(^^T, meta::access_context::unchecked()))
        if (is_route_member(m)) ++n;
    return n;
}

template <class T>
consteval meta::info nth_route(std::size_t idx) {
    std::size_t seen = 0;
    for (auto m : meta::members_of(^^T, meta::access_context::unchecked())) {
        if (!is_route_member(m)) continue;
        if (seen == idx) return m;
        ++seen;
    }
    return meta::info{};
}

// 把注解 info 拷到定长数组（consteval vector 不能直接下标拼接）
template <std::size_t N>
consteval std::array<meta::info, N> copy_anns(meta::info entity, meta::info type) {
    std::array<meta::info, N> out{};
    std::size_t i = 0;
    for (auto a : meta::annotations_of_with_type(entity, type))
        if (i < N) out[i++] = a;
    return out;
}

// 第 I 个路由的 HTTP 方法
template <class T, std::size_t I>
consteval std::string_view route_method() {
    constexpr meta::info f = nth_route<T>(I);
    if (!meta::annotations_of_with_type(f, ^^fut::http_get).empty()) return "GET";
    if (!meta::annotations_of_with_type(f, ^^fut::http_post).empty()) return "POST";
    if (!meta::annotations_of_with_type(f, ^^fut::http_put).empty()) return "PUT";
    if (!meta::annotations_of_with_type(f, ^^fut::http_delete).empty()) return "DELETE";
    return {};
}

// 第 I 个路由的路径模式（constant_of 取注解常量值）。
// 注意：必须用 if constexpr —— GCC 会急切下推各分支里的拼接表达式，
// 未命中的分支若走到 constant_of(空 info) 会直接抛 std::meta::exception。
template <class T, std::size_t I>
consteval std::string_view route_pattern() {
    constexpr meta::info f = nth_route<T>(I);
    if constexpr (!meta::annotations_of_with_type(f, ^^fut::http_get).empty()) {
        constexpr auto anns = copy_anns<1>(f, ^^fut::http_get);
        return [: meta::constant_of(anns[0]) :].path.view();
    }
    else if constexpr (!meta::annotations_of_with_type(f, ^^fut::http_post).empty()) {
        constexpr auto anns = copy_anns<1>(f, ^^fut::http_post);
        return [: meta::constant_of(anns[0]) :].path.view();
    }
    else if constexpr (!meta::annotations_of_with_type(f, ^^fut::http_put).empty()) {
        constexpr auto anns = copy_anns<1>(f, ^^fut::http_put);
        return [: meta::constant_of(anns[0]) :].path.view();
    }
    else {
        constexpr auto anns = copy_anns<1>(f, ^^fut::http_delete);
        return [: meta::constant_of(anns[0]) :].path.view();
    }
}

// ---------------------------------------------------------------------------
// 编译期：参数反射
// ---------------------------------------------------------------------------

template <class T, std::size_t RI>
consteval std::size_t param_count() {
    return meta::parameters_of(nth_route<T>(RI)).size();
}

template <class T, std::size_t RI, std::size_t P>
consteval meta::info nth_param() {
    return meta::parameters_of(nth_route<T>(RI)).data()[P];
}

// 参数注解里的 key；为空则回落到参数名（identifier_of 保留参数名）。
// 调用方（make_arg）保证该参数确有 Ann 注解，故此处的拼接永不落到空 info。
template <class T, std::size_t RI, std::size_t P, class Ann>
consteval std::string_view param_key() {
    constexpr meta::info p = nth_param<T, RI, P>();
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
// 运行时：参数构造
// ---------------------------------------------------------------------------

// 标量转换：std::string 直取；其它类型用 glaze 从原始串解析
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

// 构造路由函数的第 P 个实参
template <class T, std::size_t RI, std::size_t P>
decltype(auto) make_arg(const ::fut::request& req, const path_match& pm) {
    constexpr meta::info p = nth_param<T, RI, P>();
    // GCC: 拼接需先经别名成为类型，再作模板实参（不能直接放模板实参位）
    using param_type = [: meta::type_of(p) :];
    using Bare = std::remove_cvref_t<param_type>;

    // 逃生舱：const fut::request& 直接传原始请求
    if constexpr (std::is_same_v<Bare, ::fut::request>) {
        return static_cast<const ::fut::request&>(req);
    }
    // 请求体 -> DTO（glaze 编译期反射解析）
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_body).empty()) {
        Bare v{};
        if (bool(glz::read_json(v, req.body)))
            throw ::fut::bind_error("invalid JSON body");
        return v;
    }
    // query 参数
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_query).empty()) {
        constexpr std::string_view key = param_key<T, RI, P, ::fut::from_query>();
        for (const auto& [kk, vv] : ::fut::parse_query(req.query))
            if (kk == key) return convert_scalar<Bare>(vv, key);
        throw ::fut::bind_error("missing query parameter: " + std::string(key));
    }
    // 路径 {占位} 段
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_path).empty()) {
        constexpr std::string_view key = param_key<T, RI, P, ::fut::from_path>();
        const std::string_view seg = pm.segment(key);
        if (seg.empty())
            throw ::fut::bind_error("missing path parameter: " + std::string(key));
        return convert_scalar<Bare>(seg, key);
    }
    // 请求头
    else if constexpr (!meta::annotations_of_with_type(p, ^^fut::from_header).empty()) {
        constexpr std::string_view key = param_key<T, RI, P, ::fut::from_header>();
        for (const auto& [hn, hv] : req.headers)
            if (hn == key) return convert_scalar<Bare>(hv, key);
        throw ::fut::bind_error("missing header: " + std::string(key));
    } else {
        static_assert(!sizeof(Bare),
                      "route param needs a binding annotation "
                      "(from_body / from_query / from_path / from_header), "
                      "or must be 'const fut::request&'");
    }
}

// ---------------------------------------------------------------------------
// 运行时：调用器与返回值包装
// ---------------------------------------------------------------------------

template <class Raw> struct await_value;
template <class V, class E>
struct await_value<boost::asio::awaitable<V, E>> { using type = V; };
template <class Raw> using await_value_t = typename await_value<Raw>::type;

template <class T, std::size_t RI, std::size_t... P>
boost::asio::awaitable<::fut::response> invoke_impl(
    T& self, const ::fut::request& req, const path_match& pm, std::index_sequence<P...>) {
    using Raw = [: meta::return_type_of(nth_route<T>(RI)) :];
    using Val = await_value_t<Raw>;
    if constexpr (std::is_same_v<Val, ::fut::response>) {
        co_return co_await (self.[: nth_route<T>(RI) :])(make_arg<T, RI, P>(req, pm)...);
    } else {
        // 返回任意 DTO：自动 glaze 序列化为 200 JSON
        co_return ::fut::json(
            co_await (self.[: nth_route<T>(RI) :])(make_arg<T, RI, P>(req, pm)...));
    }
}

template <class T, std::size_t RI>
boost::asio::awaitable<::fut::response> invoke_route(
    T& self, const ::fut::request& req, const path_match& pm) {
    constexpr std::size_t n = param_count<T, RI>();
    return invoke_impl<T, RI>(self, req, pm, std::make_index_sequence<n>{});
}

// ---------------------------------------------------------------------------
// 静态路由表 + 分发
// ---------------------------------------------------------------------------

template <class T>
struct route_entry {
    std::string_view method{};
    std::string_view pattern{};
    boost::asio::awaitable<::fut::response> (*invoke)(
        T&, const ::fut::request&, const path_match&) = nullptr;
};

template <class T>
constexpr auto make_route_table() {
    constexpr std::size_t n = route_count<T>();
    std::array<route_entry<T>, n> out{};
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((out[I] = route_entry<T>{route_method<T, I>(), route_pattern<T, I>(),
                                   &invoke_route<T, I>}),
         ...);
    }(std::make_index_sequence<n>());
    return out;
}

template <class T>
constexpr auto route_table_v = make_route_table<T>();

// app::handle 的标准实现：按 (method, path) 分发到注解路由
template <class T>
boost::asio::awaitable<::fut::response> dispatch(T& self, const ::fut::request& req) {
    for (const auto& r : route_table_v<T>) {
        if (r.method != req.method) continue;
        path_match pm;
        if (!match_pattern(r.pattern, req.path, pm)) continue;
        try {
            co_return co_await r.invoke(self, req, pm);
        } catch (const ::fut::bind_error& e) {
            co_return ::fut::error(400, e.what());
        }
    }
    co_return ::fut::error(404, "no route");
}

} // namespace refl
} // namespace fut
