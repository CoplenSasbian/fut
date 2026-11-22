#pragma once

// fut/http/error.hpp -- throwing HTTP errors from an action or a middleware.
//
// An action is a coroutine: throwing is the normal way out. Throw one of
// these to choose the status code yourself --
//
//     [[=fut::http_get("/users/{id}")]]
//     asio::awaitable<user_dto> user([[=fut::from_path("id")]] long long id) {
//         if (!auto* u = find(id)) throw fut::not_found("no user " + std::to_string(id));
//         co_return *u;
//     }
//
// and the router turns it into a response. The full mapping lives in the
// router, so every app behaves the same way:
//
//   fut::http_error                -> its own status, body {"error": message}
//   fut::bind_error                -> 400, body {"error": message}
//   std::exception (anything else) -> 500, body {"error": "internal server error"},
//                                    the detail goes to the log, not the client
//   unknown method                 -> 405
//   no matching route              -> 404
//
// The executable can replace the body for every status with
// fut::exec::server::on_error(); an app that wants its own shape wraps the
// response in its own middleware.

#include <stdexcept>
#include <string>
#include <string_view>

namespace fut {

// An error with an HTTP status attached. Throw it, or one of the helpers
// below, from an action or a middleware.
class http_error : public std::runtime_error {
public:
    http_error(unsigned status, std::string_view message)
        : std::runtime_error(std::string(message)), status_(status) {}

    [[nodiscard]] unsigned status() const noexcept { return status_; }

private:
    unsigned status_;
};

// Named helpers: the status is part of the name, the message is yours.
inline http_error bad_request(std::string_view message) {
    return http_error(400, message);
}
inline http_error unauthorized(std::string_view message) {
    return http_error(401, message);
}
inline http_error forbidden(std::string_view message) {
    return http_error(403, message);
}
inline http_error not_found(std::string_view message) {
    return http_error(404, message);
}
inline http_error method_not_allowed(std::string_view message) {
    return http_error(405, message);
}
inline http_error conflict(std::string_view message) {
    return http_error(409, message);
}
inline http_error payload_too_large(std::string_view message) {
    return http_error(413, message);
}
inline http_error unsupported_media_type(std::string_view message) {
    return http_error(415, message);
}
inline http_error unprocessable(std::string_view message) {
    return http_error(422, message);
}
inline http_error too_many_requests(std::string_view message) {
    return http_error(429, message);
}
inline http_error internal_error(std::string_view message) {
    return http_error(500, message);
}
inline http_error unavailable(std::string_view message) {
    return http_error(503, message);
}

} // namespace fut
