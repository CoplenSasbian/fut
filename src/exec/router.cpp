#include "router.hpp"

namespace fut::exec {

router::match router::route(std::string_view target) const {
    const auto qpos = target.find('?');
    const std::string_view path =
        (qpos == std::string_view::npos) ? target : target.substr(0, qpos);
    const std::string_view query =
        (qpos == std::string_view::npos) ? std::string_view{} : target.substr(qpos + 1);

    match best;
    std::size_t best_len = 0;
    for (const fut::app_table* t : apps_) {
        const std::string_view prefix = t->prefix;
        if (prefix.empty() || prefix.front() != '/') continue;

        const bool exact = (path == prefix);
        const bool deeper = path.size() > prefix.size()
                            && path.compare(0, prefix.size(), prefix) == 0
                            && path[prefix.size()] == '/';
        if (!exact && !deeper) continue;
        if (prefix.size() < best_len) continue;  // keep the longest (most specific) prefix

        best.app = t;
        best_len = prefix.size();
        best.query = query;
        best.rel_path = exact ? std::string_view{"/"} : path.substr(prefix.size());
        if (best.rel_path.empty()) best.rel_path = std::string_view{"/"};
    }
    return best;
}

} // namespace fut::exec
