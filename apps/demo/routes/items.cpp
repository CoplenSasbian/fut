#include "items.hpp"

#include <string>

namespace demo {

asio::awaitable<void> items_controller::hits_count(fut::response& out) {
    const auto n = hits.fetch_add(1, std::memory_order_relaxed) + 1;
    out.dto(greeting_out{"hits", static_cast<int>(n), static_cast<long long>(n)});
    co_return;
}

asio::awaitable<void> items_controller::get(long long id, fut::response& out) {
    if (id <= 0) throw fut::not_found("no item " + std::to_string(id));
    out.dto(item{id, "item-" + std::to_string(id), 1});
    co_return;
}

asio::awaitable<void> items_controller::update(long long id, item in, fut::response& out) {
    in.id = id;
    out.dto(in);
    co_return;
}

asio::awaitable<void> items_controller::remove(long long id, fut::response& out) {
    out.set_status(204).set_header("X-Deleted", std::to_string(id));
    co_return;
}

} // namespace demo
