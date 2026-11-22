#include "middleware.hpp"

#include <chrono>
#include <string>

namespace demo {

asio::awaitable<fut::response> stamp(const fut::request& req,
                                     const fut::path_match& params,
                                     const fut::http::next_fn& next) {
    const auto start = std::chrono::steady_clock::now();
    fut::response res = co_await next(req, params);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
    res.headers.emplace_back("X-Demo-App", "yes");
    res.headers.emplace_back("X-Demo-Elapsed-Us", std::to_string(micros));
    co_return res;
}

asio::awaitable<fut::response> guard(const fut::request& req,
                                     const fut::path_match& params,
                                     const fut::http::next_fn& next) {
    // A filter: /secure needs a token, and answers 401 without calling next().
    if (req.path.starts_with("/api/demo/secure") && req.header("X-Demo-Token").empty())
        co_return fut::error(401, "missing X-Demo-Token");

    // A middleware may also fail by throwing; the router maps it exactly like a
    // handler error, so the response is still a proper 429 with a JSON body.
    if (req.header("X-Demo-Burst") == "1")
        throw fut::too_many_requests("the demo is rate limiting this request");

    co_return co_await next(req, params);
}

} // namespace demo
