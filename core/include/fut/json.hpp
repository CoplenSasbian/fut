#pragma once
// fut/json.hpp -- glaze-based JSON helpers.
// Glaze serializes aggregate DTOs through compile-time reflection; no field
// lists to maintain.

#include <optional>
#include <string>
#include <string_view>
#include <fut/http/route.hpp>
#include <glaze/glaze.hpp>

namespace fut {

namespace detail {
struct error_dto { std::string error; };
}

// DTO -> JSON response
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

// error response: {"error":"..."}
inline response error(unsigned status, std::string_view message) {
    return json(detail::error_dto{std::string(message)}, status);
}

// JSON -> DTO; returns std::nullopt on parse failure
template <class T>
std::optional<T> parse(std::string_view body) {
    T value{};
    if (bool(glz::read_json(value, body))) return std::nullopt;
    return value;
}

} // namespace fut
