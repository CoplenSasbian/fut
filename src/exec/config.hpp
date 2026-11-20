#pragma once

// JSON configuration (glaze deserializes aggregates through reflection).
// Mirrors config.json in the repository root.

#include <cstdint>
#include <filesystem>
#include <string>

namespace fut::exec {

struct config {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    int threads{1};
    std::string apps_dir{"apps"};
    int request_timeout_seconds{30};
    std::string log_level{"info"};
};

// Reads a JSON config file; returns defaults when the file does not exist,
// throws std::runtime_error when it cannot be parsed.

config load_config(const std::filesystem::path& path);

} // namespace fut::exec
