#pragma once

// apps/demo/demo.hpp -- the umbrella header of the reference app.
//
// add_app(demo /api/demo demo.hpp) generates an export unit that includes this
// header (and only this one) and scans the 'demo' namespace. Everything the app
// registers arrives through the includes below, in this order:
//
//   common.hpp       the asio alias + the HTTP surface
//   dto.hpp          plain aggregates, and the all_routes() declaration
//   services.hpp     injected io_context + log group
//   middleware.hpp   the app-scoped filter
//   routes/*.hpp     one controller per file
//   routes.hpp       all_routes(), reflecting over those controllers
//
// The order matters only in that routes.hpp comes last: it reads the
// controllers declared above it.
//
// Splitting per feature is the recommendation for a real app: the scanner sees
// declarations, so a header can hold one controller and a matching .cpp can
// hold its plain definitions. This app is header-only for brevity.

#include "common.hpp"
#include "dto.hpp"
#include "services.hpp"
#include "middleware.hpp"

#include "routes/core.hpp"
#include "routes/binding.hpp"
#include "routes/services.hpp"
#include "routes/items.hpp"
#include "routes/nested.hpp"

#include "routes.hpp"
