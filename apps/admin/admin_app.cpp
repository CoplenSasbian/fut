// /api/admin —— 管理类业务 app（动态库 fut_app_admin.dll）。
// 业务函数用 C++26 注解声明路由，参数自动绑定；handle() 一行分发。
//
// 路由：
//   GET  /api/admin/ping            存活探测
//   GET  /api/admin/info            业务信息（DTO 直接返回，自动序列化）
//   GET  /api/admin/meta            C++26 std::meta 成员枚举演示
//   POST /api/admin/echo            请求体 DTO 绑定（glaze 解析）
//   GET  /api/admin/whoami          返回值 DTO 自动序列化演示

#include <string>
#include <type_traits>
#include <vector>

#include <fut/annotate.hpp>
#include <fut/app_entry.hpp>
#include <fut/json.hpp>
#include <fut/reflect.hpp>
#include <fut/refl_router.hpp>

namespace asio = boost::asio;

namespace {

// 演示 C++26 静态反射的聚合 DTO
struct demo_dto {
    std::string name{"fut"};
    int rank{1};
    bool active{true};
    std::vector<std::string> tags{"cpp26", "reflection"};
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
    std::vector<std::string> routes{"/ping", "/info", "/meta", "/echo", "/whoami"};
};

struct whoami_dto {
    std::string app{"admin"};
    std::string user{"root"};
};

class admin_app final : public fut::app {
public:
    std::string_view name() const override { return "admin"; }
    std::string_view prefix() const override { return "/api/admin"; }

    asio::awaitable<fut::response> handle(const fut::request& req) override {
        co_return co_await fut::refl::dispatch(*this, req);
    }

    // ------------------------------------------------------------------
    // 注解即路由：编译期由 std::meta 收集进静态路由表
    // ------------------------------------------------------------------

    [[=fut::http_get("/ping")]]
    asio::awaitable<fut::response> ping() {
        struct pong { bool pong{true}; std::string app{"admin"}; };
        co_return fut::json(pong{});
    }

    // 直接返回 DTO：框架自动 glaze 序列化为 200 JSON
    [[=fut::http_get("/info")]]
    asio::awaitable<info_dto> info() { co_return info_dto{}; }

    [[=fut::http_get("/meta")]]
    asio::awaitable<fut::response> meta() {
        struct member_row { std::string name; std::string value_kind; };
        struct meta_dto {
            std::vector<member_row> members;
            std::vector<std::string> names;
        };

        demo_dto d;
        meta_dto dto;
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
        co_return fut::json(dto);
    }

    // 请求体自动绑定到 DTO
    [[=fut::http_post("/echo")]]
    asio::awaitable<fut::response> echo([[=fut::from_body{}]] echo_in in) {
        echo_out out;
        out.message = in.message;
        out.doubled = in.count * 2;
        long long sum = 0;
        for (int i = 1; i <= in.count; ++i) sum += i;
        out.sum = sum;
        co_return fut::json(out);
    }

    [[=fut::http_get("/whoami")]]
    asio::awaitable<whoami_dto> whoami() { co_return whoami_dto{}; }
};

} // namespace

FUT_IMPLEMENT_APP(admin_app)
