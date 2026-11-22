#pragma once

// all_routes(): reflect over the app's controllers and read their verb
// annotations, so /info reports what was actually registered.
//
// Included last by demo.hpp -- it reads the controller types declared above.
// FUT_APP_PREFIX comes from the add_app() call, so the paths printed here are
// the paths that were registered.

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

inline std::vector<route_entry> all_routes() {
    std::vector<route_entry> out;
    collect<demo_controller>(out, "");
    collect<binding_controller>(out, "");
    collect<service_controller>(out, "");
    collect<items_controller>(out, "/items");
    collect<nested::nested_controller>(out, "/nested");
    return out;
}

} // namespace demo
