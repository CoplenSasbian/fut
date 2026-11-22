#pragma once

// A stateful controller with a class-level path: every route is registered under
// /api/demo/items. One instance per DLL, so the counter survives requests.

#include <atomic>
#include <cstdint>

#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller("/items")]] items_controller {
    std::atomic<std::uint64_t> hits{0};

    [[=fut::http_get("/hits")]]
    asio::awaitable<void> hits_count(fut::response& out);

    [[=fut::http_get("/{id}")]]
    asio::awaitable<void> get([[=fut::from_path("id")]] long long id, fut::response& out);

    [[=fut::http_put("/{id}")]]
    asio::awaitable<void> update([[=fut::from_path("id")]] long long id,
                                 [[=fut::from_body{}]] item in, fut::response& out);

    [[=fut::http_delete("/{id}")]]
    asio::awaitable<void> remove([[=fut::from_path("id")]] long long id, fut::response& out);
};

} // namespace demo
