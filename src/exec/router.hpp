#pragma once

// Prefix router over loaded app tables: "/api/admin/x" is dispatched to the
// table registered under the longest matching prefix.

#include <span>
#include <string_view>
#include <vector>

#include <fut/http/route.hpp>

namespace fut::exec {

class router {
public:
    struct match {
        const fut::app_table* app = nullptr;  // nullptr when nothing matched
        std::string_view rel_path{};           // sub-path after the prefix, starts with '/'; "/" on exact hit
        std::string_view query{};
    };

    // Takes no ownership; the table must outlive the server.
    void add(const fut::app_table* t) { if (t) apps_.push_back(t); }

    match route(std::string_view target) const;

    std::span<const fut::app_table* const> apps() const { return apps_; }

private:
    std::vector<const fut::app_table*> apps_;
};

} // namespace fut::exec
