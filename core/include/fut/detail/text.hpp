#pragma once

// fut/detail/text.hpp -- a small structural string for annotation values.
//
// An annotation value must be structural, and std::string_view is not (it holds
// a pointer), so annotations carry their text in this fixed array. It is
// usable in a constant expression and readable at run time through view().

#include <cstddef>
#include <string_view>

namespace fut::detail {

template <std::size_t Max>
struct text_buf {
    char chars[Max]{};

    constexpr text_buf() = default;
    constexpr text_buf(std::string_view s) {
        for (std::size_t i = 0; i < s.size() && i + 1 < Max; ++i) chars[i] = s[i];
    }
    constexpr std::size_t size() const {
        std::size_t n = 0;
        while (n < Max && chars[n]) ++n;
        return n;
    }
    constexpr std::string_view view() const { return {chars, size()}; }
};

} // namespace fut::detail
