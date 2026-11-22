#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fut/platform.hpp>

#include <filesystem>
#include <string>

TEST_CASE("the platform layer names its libraries") {
    const auto suffixes = fut::platform::library_suffixes();
    REQUIRE_FALSE(suffixes.empty());

    // what the running platform uses, and that the predicate agrees with it
    bool found_native = false;
    for (const std::string_view suffix : suffixes) {
        const std::string name = std::string("fut_app_demo") + std::string(suffix);
        CHECK(fut::platform::is_library_name(name));
        found_native = found_native || suffix == ".dll" || suffix == ".so" ||
                       suffix == ".dylib";
    }
    CHECK(found_native);

    CHECK_FALSE(fut::platform::is_library_name("fut_app_demo.lib"));
    CHECK_FALSE(fut::platform::is_library_name("notes.txt"));
    CHECK(fut::platform::is_library_name("FUT_APP_DEMO" + std::string(suffixes[0])));
}

TEST_CASE("the platform layer finds the running executable") {
    const auto exe = fut::platform::executable_path();
    REQUIRE_FALSE(exe.empty());
    std::error_code ec;
    CHECK(std::filesystem::is_regular_file(exe, ec));
    CHECK_FALSE(ec);
}

TEST_CASE("opening something that is not a library reports why") {
    std::string why;
    auto lib = fut::platform::library::open("definitely-not-here/none.dll", why);
    CHECK_FALSE(lib.valid());
    CHECK_FALSE(why.empty());
    CHECK(lib.symbol("anything") == nullptr);

    auto moved = std::move(lib);
    CHECK_FALSE(moved.valid());
    moved.close();
}

TEST_CASE("the clock helpers agree with the C library") {
    const std::time_t now = 1'700'000'000;
    const std::tm utc = fut::platform::utc_time(now);
    const std::tm local = fut::platform::local_time(now);
    CHECK(utc.tm_year > 0);
    CHECK(local.tm_year > 0);
    CHECK(utc.tm_year == local.tm_year);
}
