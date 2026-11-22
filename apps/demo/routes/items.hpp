#pragma once

// A stateful controller with a class-level path: every route here is registered
// under /api/demo/items. One instance per DLL, so the counter survives across
// requests.
//
// GET /hits is declared before GET /{id} on purpose: the literal pattern is
// tried first, so /hits wins over the wildcard.

#include <atomic>
#include <cstdint>
#include <string>

#include "../common.hpp"

#include "../dto.hpp"

namespace demo {

struct [[=fut::controller("/items")]] items_controller {
    std::atomic<std::uint64_t> hits{0};

    [[=fut::http_get("/hits")]]
    asio::awaitable<greeting_out> hits_count() {
        const auto n = hits.fetch_add(1, std::memory_order_relaxed) + 1;
        co_return greeting_out{"hits", static_cast<int>(n), static_cast<long long>(n)};
    }

    [[=fut::http_get("/{id}")]]
    asio::awaitable<item> get([[=fut::from_path("id")]] long long id) {
        if (id <= 0) throw fut::not_found("no item " + std::to_string(id));
        co_return item{id, "item-" + std::to_string(id), 1};
    }

    // from_path and from_body together
    [[=fut::http_put("/{id}")]]
    asio::awaitable<item> update([[=fut::from_path("id")]] long long id,
                                 [[=fut::from_body{}]] item in) {
        in.id = id;
        co_return in;
    }

    // DELETE answering 204 with no body
    [[=fut::http_delete("/{id}")]]
    asio::awaitable<fut::response> remove([[=fut::from_path("id")]] long long id) {
        fut::response res;
        res.status = 204;
        res.headers.emplace_back("X-Deleted", std::to_string(id));
        co_return res;
    }
};

} // namespace demo
