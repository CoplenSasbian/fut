#pragma once
// fut/app.hpp —— exec 与各 app 动态库之间的稳定接口（ABI 契约）。
//
// 约定：
//  * 所有模块用同一套编译器/运行时构建（本仓库的 CMakePresets 已保证）。
//  * request 的全部 string_view 指向 exec 拥有的请求缓冲，只在 handle() 执行期间有效。
//  * response 由 app 构造，所有权移交给 exec；std::string 在同一 CRT 下跨模块释放安全。
//  * handle() 中抛出的异常由 exec 捕获并转成 500。

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>

namespace fut {

// 交给 app 处理的请求（全部只读视图）
struct request {
    std::string_view method{};   // "GET" / "POST" ...
    std::string_view target{};   // 原始请求目标（含 query）
    std::string_view path{};     // 相对 app prefix 的子路径，如 "/ping"；恰好命中 prefix 时为 "/"
    std::string_view query{};    // "?" 之后的内容
    std::string_view body{};     // 请求体
    std::span<const std::pair<std::string_view, std::string_view>> headers{};
};

// app 返回的响应
struct response {
    unsigned status = 200;
    std::string content_type = "application/json";
    std::string body{};
    std::vector<std::pair<std::string, std::string>> headers{};

    response() = default;
    response(unsigned s, std::string ct, std::string b)
        : status(s), content_type(std::move(ct)), body(std::move(b)) {}
};

// 业务 app 接口：一个 DLL 实现一个业务前缀（如 "/api/admin"）
class app {
public:
    virtual ~app() = default;

    virtual std::string_view name() const = 0;    // "admin"
    virtual std::string_view prefix() const = 0;  // "/api/admin"

    // 处理一条请求；exec 在会话协程里 co_await 本函数
    virtual boost::asio::awaitable<response> handle(const request& req) = 0;
};

} // namespace fut
