#pragma once

// App-scoped middleware, registered by annotation.
//
// A middleware receives the request and the response the action will answer
// into: it may wrap next(), short-circuit it, or fail by throwing. There is one
// response object per request and everybody writes into it.
//
// The three shapes:
//   * stamp  -- rewriting: awaits next(), then decorates the response;
//   * guard  -- filtering: fills the response and returns without next();
//   * guard  -- failing: throws fut::http_error, mapped like a handler error.

#include "common.hpp"

namespace demo {

[[=fut::filter{}]]
asio::awaitable<void> stamp(const fut::request& req, const fut::path_match& params,
                            fut::response& out, const fut::http::next_fn& next);

[[=fut::filter{}]]
asio::awaitable<void> guard(const fut::request& req, const fut::path_match& params,
                            fut::response& out, const fut::http::next_fn& next);

} // namespace demo
