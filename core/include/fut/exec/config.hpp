#pragma once

// JSON configuration (glaze deserializes aggregates through reflection).
// Mirrors config.json in the repository root.
//
// The logging section is a LIST OF CHAINS. Each chain describes one
// formatter + one sink, so adding a second file, a JSON stream or a
// debug-only console chain is a config edit -- no code change:
//
//   "log": {
//     "chains": [
//       { "sink": "console", "level": "info",
//         "formatter": "pattern", "pattern": "{T} [{L:<8}] {g:<12}] {m}",
//         "color": true },
//       { "sink": "file", "level": "trace",
//         "formatter": "json",
//         "path": "logs/fut.jsonl", "max_bytes": 2097152, "max_files": 3 }
//     ]
//   }
//
// Field meanings:
//   level      trace | debug | info | warning | error | critical | off
//   formatter  pattern | json
//   pattern    format string, used when formatter == "pattern"
//   sink       console | file
//   color      console only: ANSI colours
//   path       file only: sink path (parent directories are created)
//   max_bytes  file only: rotate above this size (0 disables rotation)
//   max_files  file only: backups kept as <path>.1 ... <path>.<n-1>
//
// An absent "log" section (or absent "chains") means the stock set: a
// colour console at info plus a rotating file at trace. An explicit
// "chains": [] means no logging at all.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <fut/log.hpp>

namespace fut::exec {

struct log_chain_config {
    std::string level{"info"};
    std::string formatter{"pattern"};
    std::string pattern{"{T} [{L:<8}] {g:<12} {m}"};
    std::string sink{"console"};
    bool color{true};
    std::string path{"logs/fut.log"};
    std::uint64_t max_bytes{2ull * 1024 * 1024};
    std::uint32_t max_files{3};
};

struct log_config {
    std::vector<log_chain_config> chains{
        log_chain_config{},
        log_chain_config{.level = "trace",
                         .pattern = "{T} [{L:<8}] {g:<12} {s}: {m}",
                         .sink = "file"},
    };
};

struct config {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    int threads{1};
    std::string apps_dir{"apps"};
    int request_timeout_seconds{30};
    std::string log_level{"info"};

    log_config log{};
};

// Reads a JSON config file; returns defaults when the file does not exist,
// throws std::runtime_error when it cannot be parsed.

FUT_API config load_config(const std::filesystem::path& path);

// Turns the logging section into chain options, validating every field.
// Throws std::runtime_error naming the offending chain and the allowed
// values, e.g.
//   log.chains[1]: unknown formatter "xml" (expected "pattern" or "json")

FUT_API std::vector<fut::log::chain_options> build_log_chains(const log_config& cfg);

} // namespace fut::exec
