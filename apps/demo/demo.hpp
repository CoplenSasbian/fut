#pragma once

// apps/demo/demo.hpp -- the umbrella header of the reference app.
//
// add_app(demo /api/demo demo.hpp ...*.cpp) generates exactly one scanning
// translation unit, which includes this header; every other .cpp in the app is
// an ordinary definition unit compiled and linked into the same DLL.
//
// That is the split this app demonstrates:
//
//   headers  the annotated DECLARATIONS -- controllers, the injected services,
//            the middleware registration -- included here in a fixed order;
//   .cpp     the bodies, with no attributes at all.
//
// Reflection works on declarations, so no definition has to be visible to the
// scan; the generated unit's calls resolve through the linker.
//
// Adding a controller means adding one header here (so the scan sees it) and
// one .cpp to add_app() (so the bodies are compiled).

#include "common.hpp"
#include "dto.hpp"

#include "services.hpp"
#include "middleware.hpp"

#include "routes/core.hpp"
#include "routes/binding.hpp"
#include "routes/runtime.hpp"
#include "routes/items.hpp"
#include "routes/nested.hpp"

#include "routes.hpp"
