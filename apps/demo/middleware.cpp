#include "middleware.hpp"

namespace demo {

asio::awaitable<fut::response> stamp(const fut::request& req,
                                     const fut::path_match& params,
                                     const fut::http::next_fn& next) {
    fut::response res = co_await next(req, params);
    res.headers.emplace_back("X-Demo-App", "yes");
    co_return res;
}

} // namespace demo
