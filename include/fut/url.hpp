#pragma once
// fut/url.hpp —— 极简 URL 工具：query 解析 + %XX / '+' 解码。

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fut {

inline std::string url_decode(std::string_view s) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') { out.push_back(' '); continue; }
        out.push_back(s[i]);
    }
    return out;
}

// "a=1&b=2" -> [("a","1"),("b","2")]
inline std::vector<std::pair<std::string, std::string>> parse_query(std::string_view q) {
    std::vector<std::pair<std::string, std::string>> out;
    while (!q.empty()) {
        std::string_view kv = q;
        std::string_view::size_type amp = q.find('&');
        if (amp != std::string_view::npos) {
            kv = q.substr(0, amp);
            q.remove_prefix(amp + 1);
        } else {
            q = {};
        }
        if (kv.empty()) continue;
        auto eq = kv.find('=');
        if (eq == std::string_view::npos) {
            out.emplace_back(url_decode(kv), std::string{});
        } else {
            out.emplace_back(url_decode(kv.substr(0, eq)), url_decode(kv.substr(eq + 1)));
        }
    }
    return out;
}

} // namespace fut
