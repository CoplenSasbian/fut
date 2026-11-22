#pragma once

// A controller one namespace deeper: the scan descends into namespaces only.

#include "../common.hpp"
#include "../dto.hpp"

namespace demo::nested {

struct [[=fut::controller("/nested")]] nested_controller {
    [[=fut::http_get("/ping")]]
    asio::awaitable<void> ping(fut::response& out);
};

} // namespace demo::nested
