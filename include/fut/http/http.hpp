#pragma once

// fut/http/http.hpp -- one include for the fut HTTP library.
//
//     #include <fut/http/http.hpp>
//
// brings in the whole HTTP annotation stack:
//
//   fut/annotations.hpp    generic annotation queries (fut::ann::of/first/has)
//   fut/http/route.hpp     request/response, method enum, path matching
//   fut/http/router.hpp    the router module interface (add_route)
//   fut/http/bind.hpp      from_body / from_query / from_path / from_header
//   fut/http/invoke.hpp    argument binding, controller invoke, return types
//   fut/http/annotate.hpp  http_action verbs + controller
//
// JSON (fut::json / fut::parse) and URL helpers are separate headers.
//
// The app module entry point (FUT_EXPORT_APP_NS) is generic and lives in
// <fut/app.hpp>; a generated export unit includes that instead of this.
// This header only says how HTTP routes are declared.

#include <fut/http/route.hpp>
#include <fut/http/router.hpp>
#include <fut/http/bind.hpp>
#include <fut/http/invoke.hpp>
#include <fut/http/annotate.hpp>
//
