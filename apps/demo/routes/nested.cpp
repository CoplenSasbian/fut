#include "nested.hpp"

namespace demo::nested {

asio::awaitable<pong> nested_controller::ping() { co_return pong{true, "nested"}; }

} // namespace demo::nested
