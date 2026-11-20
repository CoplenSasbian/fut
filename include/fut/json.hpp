#pragma once
// fut/json.hpp —— 基于 glaze 的 JSON 便捷函数。
// glaze 对聚合 DTO 做编译期反射自动序列化（不需要手写字段映射）。

#include <optional>
#include <string>
#include <string_view>
#include <fut/app.hpp>
#include <glaze/glaze.hpp>

namespace fut {

namespace detail {
struct error_dto { std::string error; };
}

// DTO -> JSON 响应
template <class T>
response json(const T& dto, unsigned status = 200) {
    response r;
    if (bool(glz::write_json(dto, r.body))) {
        r.status = 500;
        r.body = R"({"error":"serialize failed"})";
        return r;
    }
    r.status = status;
    return r;
}

// 错误响应：{"error":"..."}
inline response error(unsigned status, std::string_view message) {
    return json(detail::error_dto{std::string(message)}, status);
}

// JSON -> DTO；解析失败返回 std::nullopt
template <class T>
std::optional<T> parse(std::string_view body) {
    T value{};
    if (bool(glz::read_json(value, body))) return std::nullopt;
    return value;
}

} // namespace fut
