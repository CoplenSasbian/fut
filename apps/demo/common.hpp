#pragma once

// The app's shared preamble: one place for the asio alias, the HTTP surface and
// the two injection annotations (fut::inject, fut::log_group).
//
// Every module includes this instead of repeating boost/asio + fut/http. The
// alias lives in a single header so a module can include it in any order and
// still spell the coroutine types plainly.

#include <boost/asio.hpp>

#include <fut/http/http.hpp>
#include <fut/inject.hpp>
#include <fut/log_inject.hpp>

namespace asio = boost::asio;
