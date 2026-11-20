// /api/admin —— 管理类业务示例 app（动态库 fut_app_admin.dll）。
// 路由：
//   GET  /api/admin/ping   存活探测
//   GET  /api/admin/info   业务信息
//   GET  /api/admin/meta   C++26 std::meta 反射演示
//   POST /api/admin/echo   JSON 解析 + 回显

#include <string>
#include <type_traits>
#include <vector>

#include <fut/app_entry.hpp>
#include <fut/json.hpp>
#include <fut/reflect.hpp>

namespace asio = boost::asio;

namespace {

// 演示 C++26 静态反射的聚合 DTO（成员名在编译期由 std::meta 枚举）
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
    std::vector<std::string> routes{"/ping", "/info", "/meta", "/echo"};
};

class admin_app final : public fut::app {
public:
    std::string_view name() const override { return "admin"; }
    std::string_view prefix() const override { return "/api/admin"; }

    asio::awaitable<fut::response> handle(const fut::request& req) override {
        if (req.path == "/ping") {
            struct pong { bool pong{true}; std::string app{"admin"}; };
            co_return fut::json(pong{});
        }
        if (req.path == "/info") {
            co_return fut::json(info_dto{});
        }
        if (req.path == "/meta") {
            co_return meta_demo();
        }
        if (req.path == "/echo" && req.method == "POST") {
            auto in = fut::parse<echo_in>(req.body);
            if (!in) co_return fut::error(400, R"(expected JSON body: {"message":"...","count":N})");
            echo_out out;
            out.message = in->message;
            out.doubled = in->count * 2;
            long long sum = 0;
            for (int i = 1; i <= in->count; ++i) sum += i;
            out.sum = sum;
            co_return fut::json(out);
        }
        co_return fut::error(404, "admin: no such route");
    }

private:
    static fut::response meta_demo() {
#if FUT_HAS_REFLECTION
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
        return fut::json(dto);
#else
        return fut::error(501, "this compiler has no std::meta support");
#endif
    }
};

} // namespace

FUT_IMPLEMENT_APP(admin_app)
