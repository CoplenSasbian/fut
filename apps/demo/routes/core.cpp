// Bodies of demo_controller. No attributes: the scan reads the declarations.

#include "core.hpp"

namespace demo {

asio::awaitable<pong> demo_controller::ping() { co_return pong{true, "demo"}; }

asio::awaitable<info_out> demo_controller::info() {
    co_return info_out{FUT_APP_NAME, FUT_APP_PREFIX, fut::abi_version, all_routes()};
}

asio::awaitable<std::string> demo_controller::text() {
    co_return "fut demo app -- no route table, no recompiled server";
}

asio::awaitable<void> demo_controller::custom(fut::response& out) {
    out.set_status(201)
       .set_json(R"({"created":true})")
       .set_header("X-Demo-Custom", "1")
       .set_header("Location", "/api/demo/ping");
    co_return;
}

asio::awaitable<greeting_out> demo_controller::query(std::string name, int count) {
    co_return greeting_out{name + "!", count * 2, count * (count + 1) / 2};
}

asio::awaitable<query_in> demo_controller::query_dto(query_in in) { co_return in; }

asio::awaitable<pong> demo_controller::header(std::string token) {
    co_return pong{!token.empty(), std::move(token)};
}

asio::awaitable<request_out> demo_controller::request_info(const fut::request& req) {
    co_return request_out{std::string(req.method), std::string(req.path),
                          std::string(req.query), req.headers.size()};
}

asio::awaitable<pong> demo_controller::secure_ping() { co_return pong{true, "secure"}; }

} // namespace demo
