#pragma once

// fut/detail/text.hpp -- a small structural string for annotation values.
//
// An annotation value must be structural, and std::string_view is not (it holds
// a pointer), so annotations carry their text in this fixed array. It is
// usable in a constant expression and readable at run time through view().

#include <cstddef>
#include <cstdint>
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

// Invalid sequences become U+FFFD, so a JSON body is always valid UTF-8 even
// when a field carried bytes from a client header verbatim.
inline std::string utf8_sanitize(std::string_view s) {
    static constexpr std::string_view replacement = "\xEF\xBF\xBD";   // U+FFFD
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t extra = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) { out.push_back(s[i++]); continue; }
        if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; }
        else { out += replacement; ++i; continue; }

        if (i + extra >= n) { out += replacement; ++i; continue; }
        bool ok = true;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        const bool overlong = (extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
                              (extra == 3 && cp < 0x10000);
        if (!ok || overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out += replacement;
            ++i;
            continue;
        }
        out.append(s.substr(i, extra + 1));
        i += extra + 1;
    }
    return out;
}

// A JSON body built by hand may contain raw control bytes (say, a query value
// that decoded to CR/LF inside a string). RFC 8259 forbids them unescaped, so
// they become \u00XX. Already-escaped text is untouched.
inline std::string json_safe(std::string_view s) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20) {
            out += "\\u00";
            out += hex[(u >> 4) & 0xF];
            out += hex[u & 0xF];
        } else {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace fut::detail

namespace fut {

// Valid UTF-8 (RFC 3629): no overlongs, no surrogates, nothing past U+10FFFF.
// Query values, form fields and paths are opaque bytes; a JSON body is not --
// RFC 8259 says it is UTF-8, so the parser checks instead of guessing.
inline bool utf8_valid(std::string_view s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        std::size_t extra = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) { ++i; continue; }
        if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; }
        else return false;
        if (i + extra >= n) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i + k] & 0x3Fu);
        }
        if (extra == 1 && cp < 0x80) return false;             // overlong
        if (extra == 2 && cp < 0x800) return false;
        if (extra == 3 && cp < 0x10000) return false;
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;        // surrogate half
        i += extra + 1;
    }
    return true;
}

} // namespace fut
