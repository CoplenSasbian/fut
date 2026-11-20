#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <type_traits>
#include <vector>

#include <fut/reflect.hpp>

namespace {

struct sample {
    int id{};
    std::string tag{};
    std::vector<double> vals{};
};

} // namespace

TEST_CASE("C++26 std::meta 静态反射") {
    CHECK(fut::reflect::member_count_v<sample> == 3);

    const auto names = fut::reflect::member_names_v<sample>;
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "id");
    CHECK(names[1] == "tag");
    CHECK(names[2] == "vals");

    const sample s{7, "x", {1.5, 2.5}};
    std::vector<std::string> seen;
    fut::reflect::for_each_member(s, [&](std::string_view name, const auto& value) {
        seen.emplace_back(name);
        if constexpr (std::is_integral_v<std::remove_cvref_t<decltype(value)>>) {
            if (name == "id") CHECK(value == 7);
        }
    });
    REQUIRE(seen.size() == 3);
    CHECK(seen[0] == "id");
    CHECK(seen[1] == "tag");
    CHECK(seen[2] == "vals");
}
