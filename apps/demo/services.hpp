#pragma once

// Services the server publishes, injected while the app registers.
//
//   fut::inject      fills a pointer: the pointee type is the lookup key
//   fut::log_group   builds a group_ref bound to the server's log service
//
// Both annotations are scannable, so they work on namespace-scope variables.
// The objects belong to the server and outlive this DLL.

#include "common.hpp"

#include <fut/log.hpp>

namespace demo {

// The server's io_context: post work or start a timer on its loop.
[[=fut::inject{}]] boost::asio::io_context* g_io = nullptr;

// Name is the app name plus this identifier: "demo.log".
[[=fut::log_group{}]] fut::log::group_ref log;

} // namespace demo
