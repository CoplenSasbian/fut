#include "middleware.hpp"

#include <chrono>
#include <string>

namespace demo {

asio::awaitable<void> stamp(const fut::request& req, const fut::path_match& params,
                            fut::response& out, const fut::http::next_fn& next) {
    const auto start = std::chrono::steady_clock::now();
    co_await next(req, params, out);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
    if (!out.started()) {   // a streamed response already has its head out
        out.set_header("X-Demo-App", "yes");
        out.set_header("X-Demo-Elapsed-Us", std::to_string(micros));
    }
}

asio::awaitable<void> guard(const fut::request& req, const fut::path_match& params,
                            fut::response& out, const fut::http::next_fn& next) {
    if (req.path.starts_with("/api/demo/secure") && req.header("X-Demo-Token").empty()) {
        out.set_status(401).set_json(R"({"error":"missing X-Demo-Token"})");
        co_return;
    }
    if (req.header("X-Demo-Burst") == "1")
        throw fut::too_many_requests("the demo is rate limiting this request");

    co_await next(req, params, out);
}

} // namespace demo
