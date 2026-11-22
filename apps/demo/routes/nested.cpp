#include "nested.hpp"

namespace demo::nested {

asio::awaitable<void> nested_controller::ping(fut::response& out) {
    out.dto(pong{true, "nested"});
    co_return;
}

} // namespace demo::nested
