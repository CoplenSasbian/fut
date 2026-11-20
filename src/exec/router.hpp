#pragma once

#include <span>
#include <string_view>
#include <vector>

#include <fut/app.hpp>

namespace fut::exec {

// 前缀路由：把 "/api/admin/xxx" 分发给 prefix 为 "/api/admin" 的 app
class router {
public:
    struct match {
        fut::app* app = nullptr;    // 未命中时为 nullptr
        std::string_view rel_path{}; // 相对 prefix 的子路径，以 '/' 开头；恰好在 prefix 上时为 "/"
        std::string_view query{};
    };

    // 注册 app（不接管所有权）
    void add(fut::app* a) { if (a) apps_.push_back(a); }

    // target: 原始请求目标（可带 query）
    match route(std::string_view target) const;

    std::span<const fut::app* const> apps() const { return apps_; }

private:
    std::vector<fut::app*> apps_;
};

} // namespace fut::exec
