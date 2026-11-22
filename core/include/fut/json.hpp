#pragma once
// fut/json.hpp -- glaze-based JSON helpers.
// Glaze serializes aggregate DTOs through compile-time reflection; no field
// lists to maintain.

#include <optional>
#include <string>
#include <string_view>
#include <fut/detail/text.hpp>
#include <fut/http/route.hpp>
#include <glaze/glaze.hpp>

namespace fut {

template <class T>
response& response::dto(const T& value, unsigned code) {
    std::string payload;
    if (bool(glz::write_json(value, payload))) {
        status = 500;
        content_type = "application/json";
        body = R"({"error":"serialize failed"})";
        return *this;
    }
    if (!utf8_valid(payload)) payload = detail::utf8_sanitize(payload);   // JSON is UTF-8
    return set_json(std::move(payload), code);   // code 0 keeps the status
}

// The body as a DTO. Declared in route.hpp (which stays glaze-free) and
// defined here, so a request can parse itself wherever JSON is available.
template <class T>
std::optional<T> request::json() const {
    // Query values, form fields and paths are opaque bytes; a JSON body is not
    // (RFC 8259): it must be UTF-8, so an invalid one is rejected here instead
    // of being handed to the parser.
    if (!utf8_valid(body)) return std::nullopt;
    T value{};
    if (glz::read_json(value, body)) return std::nullopt;
    return value;
}

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
