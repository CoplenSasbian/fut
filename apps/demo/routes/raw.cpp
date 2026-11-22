#include "raw.hpp"

namespace demo {

asio::awaitable<fut::response> raw_controller::echo(const fut::request& req) {
    fut::response res;
    res.set_status(200).set_type("application/json");

    // Nothing here came from a binding: the whole request is inspectable.
    const std::string method{req.method};
    const std::string path{req.path};
    const std::string remote{req.remote};
    const std::string user = req.param_or("user", "anonymous");
    const std::string agent = std::string(req.header_or("User-Agent", "unknown"));
    const bool has_body = !req.empty_body();
    const std::size_t header_count = req.headers.size();

    char buffer[512];
    std::snprintf(buffer, sizeof(buffer),
                  R"({"method":"%s","path":"%s","user":"%s","agent":"%s",)"
                  R"("has_body":%s,"headers":%zu,"remote":"%s"})",
                  method.c_str(), path.c_str(), user.c_str(), agent.c_str(),
                  has_body ? "true" : "false", header_count, remote.c_str());
    co_return res.set_body(buffer);
}

asio::awaitable<fut::response> raw_controller::create(const fut::request& req,
                                                      const fut::path_match& params) {
    // A path segment, a query value, a header and a JSON body -- read by hand.
    const std::string_view id = params.segment("id");
    if (id.empty()) co_return fut::response::json(R"({"error":"missing id"})", 400);

    const auto body = req.json<item>();
    if (!body.has_value()) co_return fut::response::json(R"({"error":"invalid JSON"})", 400);

    const std::string trace = req.param_or("trace", "none");
    const std::string token = std::string(req.header_or("X-Demo-Token", ""));

    item created = *body;
    if (const auto parsed = std::strtoll(id.data(), nullptr, 10); parsed != 0) created.id = parsed;

    fut::response res = fut::json(created, 201);
    res.set_header("X-Trace", trace)
       .set_header("X-Token-Seen", token.empty() ? "no" : "yes");

    co_return res;
}

asio::awaitable<fut::response> raw_controller::created() {
    // Status, media type, headers and a cookie without any helper type.
    fut::response res = fut::response::html("<h1>created</h1>", 201);
    res.set_header("X-Demo-Created", "1")
       .set_cookie("session", "abc123", "Path=/; HttpOnly; SameSite=Lax");
    co_return res;
}

} // namespace demo
