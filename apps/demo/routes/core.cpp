#include "core.hpp"

#include <string>

namespace demo {

asio::awaitable<void> demo_controller::ping(fut::response& out) {
    out.dto(pong{true, "demo"});
    co_return;
}

asio::awaitable<void> demo_controller::info(fut::response& out) {
    out.dto(info_out{FUT_APP_NAME, FUT_APP_PREFIX, fut::abi_version, all_routes()});
    co_return;
}

asio::awaitable<void> demo_controller::text(fut::response& out) {
    out.set_text("fut demo app -- no route table, no recompiled server");
    co_return;
}

asio::awaitable<void> demo_controller::custom(fut::response& out) {
    out.set_status(201)
        .set_json(R"({"created":true})")
        .set_header("X-Demo-Custom", "1")
        .set_header("Location", "/api/demo/ping");
    co_return;
}

asio::awaitable<void> demo_controller::query(std::string name, int count,
                                             fut::response& out) {
    out.dto(greeting_out{name + "!", count * 2, count * (count + 1) / 2});
    co_return;
}

asio::awaitable<void> demo_controller::query_dto(query_in in, fut::response& out) {
    out.dto(in);
    co_return;
}

asio::awaitable<void> demo_controller::header(std::string token, fut::response& out) {
    out.dto(pong{!token.empty(), std::move(token)});
    co_return;
}

asio::awaitable<void> demo_controller::request_info(const fut::request& req,
                                                    fut::response& out) {
    out.dto(request_out{std::string(req.method), std::string(req.path),
                        std::string(req.query), req.headers.size()});
    co_return;
}

asio::awaitable<void> demo_controller::secure_ping(fut::response& out) {
    out.dto(pong{true, "secure"});
    co_return;
}

} // namespace demo
