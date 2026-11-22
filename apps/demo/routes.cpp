// all_routes(): what /info reports. FUT_APP_PREFIX comes from the add_app()
// call, so the paths printed here are the paths that were registered.

#include "routes.hpp"

#include "routes/binding.hpp"
#include "routes/core.hpp"
#include "routes/items.hpp"
#include "routes/nested.hpp"
#include "routes/runtime.hpp"

namespace demo {

std::vector<route_entry> all_routes() {
    std::vector<route_entry> out;
    collect<demo_controller>(out, "");
    collect<binding_controller>(out, "");
    collect<runtime_controller>(out, "");
    collect<items_controller>(out, "/items");
    collect<nested::nested_controller>(out, "/nested");
    return out;
}

} // namespace demo
