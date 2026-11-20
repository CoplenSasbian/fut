// /api/web -- definitions for the declared actions.
//
// A plain translation unit: compiled normally, linked into the same DLL as
// the generated export unit. The annotations stay in the header next to the
// declarations; nothing here mentions routes.

#include <chrono>
#include <stdexcept>

#include "web_routes.hpp"

namespace asio = boost::asio;

namespace web {

asio::awaitable<pong_dto> ping() { co_return pong_dto{}; }

asio::awaitable<hello_out> hello(std::string name) {
    co_return hello_out{"hello, " + name};
}

asio::awaitable<time_out> time() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    co_return time_out{std::chrono::duration_cast<std::chrono::seconds>(now).count()};
}

asio::awaitable<calc_out> calc(calc_in in) {
    if (in.op == "add") co_return calc_out{in.a + in.b, in.op};
    if (in.op == "sub") co_return calc_out{in.a - in.b, in.op};
    if (in.op == "mul") co_return calc_out{in.a * in.b, in.op};
    if (in.op == "div") {
        if (in.b == 0.0) throw fut::bind_error("division by zero");
        co_return calc_out{in.a / in.b, in.op};
    }
    throw fut::bind_error("unknown op: " + in.op);
}

asio::awaitable<sum_out> sum(long long a, long long b) {
    co_return sum_out{a, b, a + b};
}

asio::awaitable<std::string> note() {
    co_return "fut web app -- string actions are served as plain text";
}

namespace beta {

asio::awaitable<std::string> beta_note() {
    co_return "from namespace web::beta";
}

} // namespace beta

} // namespace web
