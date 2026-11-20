#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace fut::exec {

// 与仓库根 config.json 对应（glaze 自动反射读写）
struct config {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    int threads{1};
    std::string apps_dir{"apps"};
    int request_timeout_seconds{30};
};

// 读取 JSON 配置；文件不存在时返回默认值，格式错误抛 std::runtime_error。
config load_config(const std::filesystem::path& path);

} // namespace fut::exec
