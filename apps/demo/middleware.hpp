#pragma once

// App-scoped middleware, registered by annotation.
//
// A middleware is a coroutine that either answers the request itself or
// continues the chain with next(). fut::filter takes the function and hands it
// to the router while the app loads -- the app writes no registration code.
//
// Declaration order is run order: stamp is outermost, so it sees the response
// of everything inside it, guard included.
//
// The three shapes worth copying:
//   * stamp  -- rewriting: calls next(), then adds headers to the response;
//   * guard  -- filtering: answers 401 itself, without calling next();
//   * guard  -- failing: throws fut::http_error, which the router maps like a
//               handler error (the client still gets a proper status and body).
//
// Scope: these run for the routes of THIS app only, after the executable's
// platform middleware. A 404 belongs to no app, so they do not run at all.

#include "common.hpp"

namespace demo {

[[=fut::filter{}]]
asio::awaitable<fut::response> stamp(const fut::request& req,
                                     const fut::path_match& params,
                                     const fut::http::next_fn& next);

[[=fut::filter{}]]
asio::awaitable<fut::response> guard(const fut::request& req,
                                     const fut::path_match& params,
                                     const fut::http::next_fn& next);

} // namespace demo
