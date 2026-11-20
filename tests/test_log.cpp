#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <fut/log.hpp>

TEST_SUITE("logging") {

TEST_CASE("reflection-based level names") {
    CHECK(fut::log::to_string(fut::log::level::info) == "info");
    CHECK(fut::log::to_string(fut::log::level::warning) == "warning");
    CHECK(fut::log::to_string(fut::log::level::critical) == "critical");
    CHECK(fut::log::to_string(fut::log::level::off) == "off");
    CHECK(fut::log::from_string<fut::log::level>("debug").value() ==
          fut::log::level::debug);
    CHECK(fut::log::from_string<fut::log::level>("trace").value() ==
          fut::log::level::trace);
    CHECK_FALSE(fut::log::from_string<fut::log::level>("nope").has_value());
}

TEST_CASE("file chain writes, level filter drops, flush persists") {
    const std::filesystem::path out = "log_test_out.log";
    std::error_code ignored;
    std::filesystem::remove(out, ignored);

    const auto id = fut::log::add_chain(fut::log::chain_options{
        .minimum_level = fut::log::level::trace,
        .formatter = fut::log::pattern_formatter{"[{L:<8}] {g}: {m}"},
        .sink = fut::log::file_sink{.path = out, .max_bytes = 0, .max_files = 1},
    });
    REQUIRE(id != 0);

    constexpr auto test_log = fut::log::grp("test");
    test_log.info("hello {}", 42);
    test_log.warning("careful");

    // Below the global level: dropped before formatting.
    fut::log::set_level(fut::log::level::critical);
    test_log.error("dropped before formatting");
    fut::log::set_level(fut::log::level::trace);

    fut::log::flush();

    std::ifstream in(out);
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    CHECK(text.find("[INFO    ] test: hello 42") != std::string::npos);
    CHECK(text.find("[WARNING ] test: careful") != std::string::npos);
    CHECK(text.find("dropped before formatting") == std::string::npos);

    fut::log::remove_chain(id);
    std::filesystem::remove(out, ignored);
    fut::log::shutdown();
}

TEST_CASE("a broken pattern throws std::runtime_error") {
    CHECK_THROWS_AS(
        (fut::log::add_chain(fut::log::chain_options{
            .formatter = fut::log::pattern_formatter{"{nope}"},
            .sink = fut::log::console_sink{},
        })),
        std::runtime_error);
    fut::log::shutdown();
}

} // TEST_SUITE
