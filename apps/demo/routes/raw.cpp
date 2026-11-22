#include "raw.hpp"

#include <cstdlib>
#include <string>

namespace demo {

asio::awaitable<void> raw_controller::echo(const fut::request& req, fut::response& out) {
    const std::string method{req.method};
    const std::string path{req.path};
    const std::string remote{req.remote};
    const std::string user = req.param_or("user", "anonymous");
    const std::string agent = std::string(req.header_or("User-Agent", "unknown"));
    const bool has_body = !req.empty_body();

    std::string payload;
    payload.reserve(256);
    payload.append(R"({"method":")").append(method).append(R"(","path":")").append(path);
    payload.append(R"(","user":")").append(user).append(R"(","agent":")").append(agent);
    payload.append(R"(","has_body":)").append(has_body ? "true" : "false");
    payload.append(R"(,"headers":)").append(std::to_string(req.headers.size()));
    payload.append(R"(,"remote":")").append(remote).append(R"("})");

    out.set_json(std::move(payload));
    co_return;
}

asio::awaitable<void> raw_controller::create(const fut::request& req,
                                             const fut::path_match& params,
                                             fut::response& out) {
    const std::string_view id = params.segment("id");
    if (id.empty()) {
        out.set_status(400).set_json(R"({"error":"missing id"})");
        co_return;
    }

    const auto body = req.json<item>();
    if (!body.has_value()) {
        out.set_status(400).set_json(R"({"error":"invalid JSON"})");
        co_return;
    }

    item created = *body;
    if (const auto parsed = std::strtoll(id.data(), nullptr, 10); parsed != 0) created.id = parsed;

    const std::string trace = req.param_or("trace", "none");
    const std::string token = std::string(req.header_or("X-Demo-Token", ""));

    out.set_status(201)
        .dto(created)
        .set_header("X-Trace", trace)
        .set_header("X-Token-Seen", token.empty() ? "no" : "yes");
    co_return;
}

asio::awaitable<void> raw_controller::created(fut::response& out) {
    out.set_status(201)
        .set_html("<h1>created</h1>")
        .set_header("X-Demo-Created", "1")
        .set_cookie("session", "abc123", "Path=/; HttpOnly; SameSite=Lax");
    co_return;
}

} // namespace demo
