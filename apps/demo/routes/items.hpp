#pragma once

// A stateful controller with a class-level path: every route here is registered
// under /api/demo/items. One instance per DLL, so the counter survives across
// requests.
//
// GET /hits is declared before GET /{id} on purpose: the literal pattern is
// tried first, so /hits wins over the wildcard.

#include <atomic>
#include <cstdint>

#include "../common.hpp"
#include "../dto.hpp"

namespace demo {

struct [[=fut::controller("/items")]] items_controller {
    std::atomic<std::uint64_t> hits{0};

    [[=fut::http_get("/hits")]]
    asio::awaitable<greeting_out> hits_count();

    [[=fut::http_get("/{id}")]]
    asio::awaitable<item> get([[=fut::from_path("id")]] long long id);

    // from_path and from_body together
    [[=fut::http_put("/{id}")]]
    asio::awaitable<item> update([[=fut::from_path("id")]] long long id,
                                 [[=fut::from_body{}]] item in);

    // DELETE answering 204 with no body
    [[=fut::http_delete("/{id}")]]
    asio::awaitable<void> remove([[=fut::from_path("id")]] long long id,
                                fut::response& out);
};

} // namespace demo
