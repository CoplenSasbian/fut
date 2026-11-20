#include "router.hpp"

namespace fut::exec {

void router::add_route(fut::method m, std::string_view pattern, fut::http::route_fn fn) {
    entry e;
    e.method = m;
    e.pattern.assign(pattern);
    e.invoke = fn;
    if (current_app_ < apps_.size()) {
        e.app = apps_[current_app_].name;
        ++apps_[current_app_].routes;
    }
    entries_.push_back(std::move(e));
}

void router::add_route_thunk(void* self, fut::method m, std::string_view pattern,
                             fut::http::route_fn fn) {
    static_cast<router*>(self)->add_route(m, pattern, fn);
}

void router::begin_app(std::string_view name, std::string_view prefix) {
    apps_.push_back(app_info{std::string(name), std::string(prefix), 0});
    current_app_ = apps_.size() - 1;
}

void router::end_app() { current_app_ = npos; }

std::optional<router::hit> router::match(fut::method m, std::string_view path) const {
    const entry* best = nullptr;
    fut::path_match best_pm{};
    std::size_t best_score = 0;
    for (const entry& e : entries_) {
        if (e.method != m) continue;
        fut::path_match pm;
        if (!fut::match_pattern(e.pattern, path, pm)) continue;
        const std::size_t score = fut::literal_segments(e.pattern);
        if (!best || score > best_score) {
            best = &e;
            best_pm = pm;
            best_score = score;
        }
    }
    if (!best) return std::nullopt;
    return hit{best, best_pm};
}

} // namespace fut::exec
