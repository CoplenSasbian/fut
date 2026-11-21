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

    // Logging: patterns are caller data, not constants baked into the server.
    std::string log_pattern{"{T} [{L:<8}] {g:<12} {m}"};
    std::string log_file_pattern{"{T} [{L:<8}] {g:<12} {s}: {m}"};
    std::string log_file{"logs/fut.log"};
    std::uint64_t log_file_max_bytes{2ull * 1024 * 1024};
    std::uint32_t log_file_max_files{3};
};

// Reads a JSON config file; returns defaults when the file does not exist,
// throws std::runtime_error when it cannot be parsed.

config load_config(const std::filesystem::path& path);

} // namespace fut::exec
