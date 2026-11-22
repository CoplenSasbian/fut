#include "items.hpp"

namespace demo {

asio::awaitable<greeting_out> items_controller::hits_count() {
    const auto n = hits.fetch_add(1, std::memory_order_relaxed) + 1;
    co_return greeting_out{"hits", static_cast<int>(n), static_cast<long long>(n)};
}

asio::awaitable<item> items_controller::get(long long id) {
    if (id <= 0) throw fut::not_found("no item " + std::to_string(id));
    co_return item{id, "item-" + std::to_string(id), 1};
}

asio::awaitable<item> items_controller::update(long long id, item in) {
    in.id = id;
    co_return in;
}

asio::awaitable<item> items_controller::patch(long long id, item in) {
    in.id = id;
    if (in.name.empty()) in.name = "patched";
    co_return in;
}

asio::awaitable<void> items_controller::remove(long long id, fut::response& out) {
    out.set_status(204).set_header("X-Deleted", std::to_string(id));
    co_return;
}

} // namespace demo
