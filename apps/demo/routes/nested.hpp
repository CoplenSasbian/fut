#pragma once

// A controller one namespace deeper. The scan descends into namespaces (and
// only into namespaces), so grouping controllers by feature costs nothing and
// no list of controllers is ever written by hand.

#include "../common.hpp"
#include "../dto.hpp"

namespace demo::nested {

struct [[=fut::controller("/nested")]] nested_controller {
    [[=fut::http_get("/ping")]]
    asio::awaitable<pong> ping();
};

} // namespace demo::nested
