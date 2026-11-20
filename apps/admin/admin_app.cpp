// /api/admin -- sample business app (fut_app_admin.dll).
//
// Actions are plain free functions; their annotations are the routes and the
// parameter bindings. The compiled table is what the DLL exports.

#include <string>
#include <type_traits>
#include <vector>

#include <fut/annotate.hpp>
#include <fut/json.hpp>
#include <fut/reflect.hpp>
#include <fut/refl_router.hpp>

namespace asio = boost::asio;

namespace {

// ---- DTOs (glaze serializes aggregates via compile-time reflection) ----

struct pong_dto { bool pong{true}; std::string app{"admin"}; };

// Demonstrates C++26 std::meta member enumeration.
struct demo_dto {
    std::string name{"fut"};
    int rank{1};
    bool active{true};
    std::vector<std::string> tags{"cpp26", "reflection"};
};

struct member_row { std::string name; std::string value_kind; };
struct meta_dto {
    std::vector<member_row> members;
    std::vector<std::string> names;
};

struct echo_in {
    std::string message{};
    int count{0};
};

struct echo_out {
    std::string message{};
    int doubled{};
    long long sum{};
};

struct info_dto {
    std::string app{"admin"};
    std::vector<std::string> routes{"/ping", "/info", "/meta", "/echo", "/whoami", "/raw"};
};

struct whoami_dto {
    std::string app{"admin"};
    std::string user{"root"};
};

// ---- actions: annotations define routes, parameters bind themselves ----

[[=fut::http_get("/ping")]]
asio::awaitable<pong_dto> ping() { co_return pong_dto{}; }

[[=fut::http_get("/info")]]
asio::awaitable<info_dto> info() { co_return info_dto{}; }

[[=fut::http_get("/meta")]]
asio::awaitable<meta_dto> meta() {
    meta_dto dto;
    demo_dto d;
    fut::reflect::for_each_member(d, [&](std::string_view name, const auto& val) {
        using V = std::remove_cvref_t<decltype(val)>;
        const char* kind = "<unknown>";
        if constexpr (std::is_same_v<V, bool>) kind = "bool";
        else if constexpr (std::is_same_v<V, std::string>) kind = "string";
        else if constexpr (std::is_same_v<V, std::vector<std::string>>) kind = "string[]";
        else if constexpr (std::is_integral_v<V>) kind = "int";
        dto.members.push_back({std::string(name), kind});
    });
    for (std::string_view n : fut::reflect::member_names_v<demo_dto>)
        dto.names.emplace_back(n);
    co_return dto;
}

[[=fut::http_post("/echo")]]
asio::awaitable<echo_out> echo([[=fut::from_body{}]] echo_in in) {
    echo_out out;
    out.message = in.message;
    out.doubled = in.count * 2;
    long long sum = 0;
    for (int i = 1; i <= in.count; ++i) sum += i;
    out.sum = sum;
    co_return out;
}

[[=fut::http_get("/whoami")]]
asio::awaitable<whoami_dto> whoami() { co_return whoami_dto{}; }

// plain std::string return -> served as text/plain
[[=fut::http_get("/raw")]]
asio::awaitable<std::string> raw() {
    co_return "fut admin app -- C++26 reflection inside";
}

} // namespace

FUT_EXPORT_APP("admin", "/api/admin", ^^ping, ^^info, ^^meta, ^^echo, ^^whoami, ^^raw)
