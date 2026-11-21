#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "config.hpp"

namespace {

// Writes a config file next to the test binary and returns its path.
std::filesystem::path write_config(std::string_view body, std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << body;
    return path;
}

} // namespace

TEST_SUITE("config file") {

TEST_CASE("missing file yields the stock chains") {
    const auto cfg = fut::exec::load_config("does-not-exist.json");
    REQUIRE(cfg.log.chains.size() == 2);
    CHECK(cfg.log.chains[0].sink == "console");
    CHECK(cfg.log.chains[0].level == "info");
    CHECK(cfg.log.chains[1].sink == "file");
    CHECK(cfg.log.chains[1].level == "trace");

    const auto chains = fut::exec::build_log_chains(cfg.log);
    REQUIRE(chains.size() == 2);
    CHECK(std::get_if<fut::log::console_sink>(&chains[0].sink) != nullptr);
    CHECK(std::get_if<fut::log::file_sink>(&chains[1].sink) != nullptr);
}

TEST_CASE("a chain list is read field by field") {
    const auto path = write_config(R"({
        "port": 9000,
        "log": {
            "chains": [
                { "sink": "console", "level": "debug",
                  "formatter": "pattern", "pattern": "[{L}] {m}", "color": false },
                { "sink": "file", "level": "trace", "formatter": "json",
                  "path": "logs/x.jsonl", "max_bytes": 4096, "max_files": 5 }
            ]
        }
    })", "fut_cfg_ok.json");

    const auto cfg = fut::exec::load_config(path);
    CHECK(cfg.port == 9000);
    REQUIRE(cfg.log.chains.size() == 2);

    const auto chains = fut::exec::build_log_chains(cfg.log);
    REQUIRE(chains.size() == 2);

    CHECK(chains[0].minimum_level == fut::log::level::debug);
    const auto* console = std::get_if<fut::log::console_sink>(&chains[0].sink);
    REQUIRE(console != nullptr);
    CHECK(console->color == false);
    const auto* console_fmt = std::get_if<fut::log::pattern_formatter>(&chains[0].formatter);
    REQUIRE(console_fmt != nullptr);
    CHECK(console_fmt->pattern == "[{L}] {m}");

    const auto* file = std::get_if<fut::log::file_sink>(&chains[1].sink);
    REQUIRE(file != nullptr);
    CHECK(file->path == "logs/x.jsonl");
    CHECK(file->max_bytes == 4096);
    CHECK(file->max_files == 5);
    CHECK(std::get_if<fut::log::json_formatter>(&chains[1].formatter) != nullptr);

    std::filesystem::remove(path);
}

TEST_CASE("an explicit empty list disables logging") {
    const auto path = write_config(R"({"log": {"chains": []}})", "fut_cfg_empty.json");
    const auto cfg = fut::exec::load_config(path);
    CHECK(cfg.log.chains.empty());
    CHECK(fut::exec::build_log_chains(cfg.log).empty());
    std::filesystem::remove(path);
}

TEST_CASE("an unknown formatter names the chain and the alternatives") {
    const auto path = write_config(R"({"log": {"chains": [
        {"sink": "console"},
        {"sink": "console", "formatter": "xml"}
    ]}})", "fut_cfg_badfmt.json");

    const auto cfg = fut::exec::load_config(path);
    try {
        (void)fut::exec::build_log_chains(cfg.log);
        FAIL("expected an exception");
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        CHECK(msg.find("log.chains[1]") != std::string::npos);
        CHECK(msg.find("xml") != std::string::npos);
        CHECK(msg.find("pattern") != std::string::npos);
        CHECK(msg.find("json") != std::string::npos);
    }
    std::filesystem::remove(path);
}

TEST_CASE("an unknown sink is rejected") {
    const auto path = write_config(R"({"log": {"chains": [{"sink": "syslog"}]}})",
                                   "fut_cfg_badsink.json");
    const auto cfg = fut::exec::load_config(path);
    try {
        (void)fut::exec::build_log_chains(cfg.log);
        FAIL("expected an exception");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("log.chains[0]") != std::string::npos);
    }
    std::filesystem::remove(path);
}

TEST_CASE("an unknown level is rejected") {
    const auto path = write_config(R"({"log": {"chains": [{"level": "verbose"}]}})",
                                   "fut_cfg_badlevel.json");
    const auto cfg = fut::exec::load_config(path);
    try {
        (void)fut::exec::build_log_chains(cfg.log);
        FAIL("expected an exception");
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        CHECK(msg.find("verbose") != std::string::npos);
        CHECK(msg.find("critical") != std::string::npos);  // the alternatives are listed
    }
    std::filesystem::remove(path);
}

TEST_CASE("a file sink without a path or with zero backups is rejected") {
    const auto no_path = write_config(R"({"log": {"chains": [{"sink": "file", "path": ""}]}})",
                                      "fut_cfg_nopath.json");
    CHECK_THROWS_AS((void)fut::exec::build_log_chains(
                        fut::exec::load_config(no_path).log),
                    std::runtime_error);
    std::filesystem::remove(no_path);

    const auto no_backup = write_config(R"({"log": {"chains": [
        {"sink": "file", "path": "x.log", "max_files": 0}
    ]}})", "fut_cfg_nobackup.json");
    CHECK_THROWS_AS((void)fut::exec::build_log_chains(
                        fut::exec::load_config(no_backup).log),
                    std::runtime_error);
    std::filesystem::remove(no_backup);
}

} // TEST_SUITE
