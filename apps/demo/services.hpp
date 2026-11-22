#pragma once

// Services the server publishes, injected while the app registers.
//
//   fut::inject      fills a pointer: the pointee type is the lookup key
//   fut::log_group   builds a group_ref bound to the server's log service
//
// Both annotations are scannable, so they work on namespace-scope variables.
// The variables are declared here (the declarations carry the annotations, so
// the scan sees them) and defined in services.cpp without attributes. The
// objects themselves belong to the server and outlive this DLL.

#include "common.hpp"

#include <fut/log.hpp>

namespace demo {

// The server's io_context: post work or start a timer on its loop. Its type is
// unique, so no name is needed.
[[=fut::inject{}]] extern boost::asio::io_context* g_io;

// No name given: the group becomes the app name plus this identifier, "demo.log".
[[=fut::log_group{}]] extern fut::log::group_ref log;

// A name given: the group is exactly that, no derivation.
[[=fut::log_group{"demo.audit"}]] extern fut::log::group_ref audit_log;

} // namespace demo
