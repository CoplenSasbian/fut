#pragma once

// The app's shared preamble: one place for the asio alias and the HTTP surface.
//
// Every module includes this instead of repeating boost/asio + fut/http. The
// alias lives in a single header so a module can include it in any order and
// still spell the coroutine types plainly.

#include <boost/asio.hpp>

#include <fut/http/http.hpp>

namespace asio = boost::asio;
