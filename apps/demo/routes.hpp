#pragma once

// Reflection over the controllers: collect() reads the verb annotations and
// composes the registered paths. The template lives in the header because it
// must be instantiated per controller type; the single all_routes() body is in
// routes.cpp.

#include <string>
#include <string_view>
#include <vector>

#include <meta>

#include <fut/http/annotate.hpp>

#include "dto.hpp"

namespace demo {

template <class T>
void collect(std::vector<route_entry>& out, std::string_view class_path) {
    template for (constexpr std::meta::info member :
                  std::define_static_array(std::meta::members_of(
                      ^^T, std::meta::access_context::unchecked()))) {
        if constexpr (std::meta::is_class_member(member) && std::meta::is_function(member)) {
            template for (constexpr std::meta::info verb_ann :
                          std::define_static_array(fut::ann::of<fut::http_action>(member))) {
                constexpr auto verb = [: std::meta::constant_of(verb_ann) :];
                out.push_back(route_entry{
                    std::string(fut::to_string(static_cast<fut::method>(verb.method_val))),
                    std::string(fut::join_path(FUT_APP_PREFIX, class_path, verb.path.view()))});
            }
        }
    }
}

} // namespace demo
