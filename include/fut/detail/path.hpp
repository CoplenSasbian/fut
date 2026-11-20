#pragma once

// Helpers that print a compile time source path relative to the project root.
// FUT_PROJECT_ROOT is injected by the top-level CMakeLists; when it is
// undefined every function returns its input unchanged.

#include <format>
#include <string>
#include <string_view>

namespace fut::detail {

// Normalizes the separator to '/'; on Windows '\' and '/' are the same character.
[[nodiscard]] constexpr char normalized_sep(char c) noexcept {
    return c == '\\' ? '/' : c;
}

[[nodiscard]] constexpr bool is_sep(char c) noexcept {
    return c == '/' || c == '\\';
}

[[nodiscard]] constexpr bool has_project_root() noexcept {
#if defined(FUT_PROJECT_ROOT)
    return true;
#else
    return false;
#endif
}

// The project root; empty when it is not configured.
[[nodiscard]] constexpr std::string_view project_root() noexcept {
#if defined(FUT_PROJECT_ROOT)
    return std::string_view{FUT_PROJECT_ROOT};
#else
    return {};
#endif
}

// Whether path is under root; compared by separator so that "src2" is not
// taken for a child of "src".
[[nodiscard]] constexpr bool starts_with_dir(std::string_view path,
                                             std::string_view root,
                                             std::string_view& rest) noexcept {
    if (root.empty() || path.size() <= root.size()) return false;
    for (std::size_t i = 0; i < root.size(); ++i) {
        if (normalized_sep(path[i]) != normalized_sep(root[i])) return false;
    }
    if (!is_sep(path[root.size()])) return false;
    rest = path.substr(root.size() + 1);
    return true;
}

// Absolute path -> path relative to the project root, with '/' separators. A path
// that is not under the root is returned as is (still normalized).
//
// The normalization is what keeps a file name identical across build systems: the
// compiler copies whatever spelling it was given into __FILE__, so Ninja hands it
// an absolute "D:/project/test/x.cpp" while the Makefile generator hands it a
// relative "test\x.cpp". Without this, the same log line reads differently
// depending on who invoked the compiler.
[[nodiscard]] inline std::string relative_path(std::string_view path) {
    std::string_view rest{};
    if (starts_with_dir(path, project_root(), rest)) path = rest;

    std::string normalized{path};
    for (char& c : normalized) c = normalized_sep(c);
    return normalized;
}

// Takes the last segment of the path, dropping the directory part.
[[nodiscard]] constexpr std::string_view base_file(std::string_view path) noexcept {
    const auto sep = path.find_last_of("/\\");
    return sep == std::string_view::npos ? path : path.substr(sep + 1);
}

// Lays out a text with a format specifier such as `{:<30}`; an invalid spec
// returns the text as is.
[[nodiscard]] inline std::string format_text(std::string_view text, std::string_view spec) {
    if (spec.empty()) return std::string(text);
    std::string pattern = "{:";
    pattern += spec;
    pattern += '}';
    try {
        return std::vformat(pattern, std::make_format_args(text));
    } catch (...) {
        return std::string(text);
    }
}

}  // namespace fut::detail
