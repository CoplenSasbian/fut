// /api/web -- definitions for the declared controller actions.
//
// A plain translation unit: compiled normally, linked into the same DLL as
// the generated export unit. No annotations here; the routes stay declared
// in the header next to the controller.

#include <chrono>
#include <stdexcept>

#include "web_routes.hpp"

namespace asio = boost::asio;

namespace web {

asio::awaitable<pong_dto> web_controller::ping() { co_return pong_dto{}; }

asio::awaitable<hello_out> web_controller::hello(std::string name) {
    co_return hello_out{"hello, " + name};
}

asio::awaitable<time_out> web_controller::time() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    co_return time_out{std::chrono::duration_cast<std::chrono::seconds>(now).count()};
}

asio::awaitable<calc_out> web_controller::calc(calc_in in) {
    if (in.op == "add") co_return calc_out{in.a + in.b, in.op};
    if (in.op == "sub") co_return calc_out{in.a - in.b, in.op};
    if (in.op == "mul") co_return calc_out{in.a * in.b, in.op};
    if (in.op == "div") {
        if (in.b == 0.0) throw fut::bind_error("division by zero");
        co_return calc_out{in.a / in.b, in.op};
    }
    throw fut::bind_error("unknown op: " + in.op);
}

asio::awaitable<sum_out> web_controller::sum(long long a, long long b) {
    co_return sum_out{a, b, a + b};
}

asio::awaitable<std::string> web_controller::note() {
    co_return "fut web app -- string actions are served as plain text";
}

namespace beta {

asio::awaitable<std::string> beta_controller::note() {
    co_return "from namespace web::beta";
}

} // namespace beta

} // namespace web
