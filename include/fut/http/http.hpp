#pragma once

// fut/http/http.hpp -- one include for the fut HTTP library.
//
//     #include <fut/http/http.hpp>
//
// brings in the whole annotation-routing stack:
//
//   fut/ctx.hpp            the fut_ctx module contract (server <-> app DLL)
//   fut/scannable.hpp      "the scanner calls my operation()"
//   fut/scan.hpp           the generic namespace scanner
//   fut/http/route.hpp     request/response, method enum, path matching
//   fut/http/router.hpp    the router module interface (add_route)
//   fut/http/bind.hpp      from_body / from_query / from_path / from_header
//   fut/http/invoke.hpp    argument binding, controller invoke, return types
//   fut/http/annotate.hpp  http_action verbs + controller
//
// JSON (fut::json / fut::parse) and URL helpers are separate headers.
//
// ---------------------------------------------------------------------------
// The app DLL export surface
// ---------------------------------------------------------------------------
//
// add_app(name, prefix, headers...) generates one translation unit that ends
// with FUT_EXPORT_APP_NS(FUT_APP_NAME, FUT_APP_PREFIX, ^^name). The macro
// emits the three exported symbols the server looks for:
//
//   fut_abi_version()            ABI check
//   fut_app_name() / _prefix()   used for logging and the status endpoint
//   fut_app_register(ctx)        the one function the server calls
//
// The prefix from add_app reaches the routes through the FUT_APP_PREFIX
// compile definition -- the DLL composes its own paths, so the executable
// never has to know which prefixes exist.
//
//     fut_app_register(ctx)
//         -> fut::scan::run<^^admin>(ctx, "/api/admin")
//             -> controller::operation<admin::users_controller>(ctx, "/api/admin")
//                 -> router->add_route(self, GET, "/api/admin/users/hits", &fn)

#include <fut/ctx.hpp>
#include <fut/scannable.hpp>
#include <fut/scan.hpp>

#include <fut/http/route.hpp>
#include <fut/http/router.hpp>
#include <fut/http/bind.hpp>
#include <fut/http/invoke.hpp>
#include <fut/http/annotate.hpp>

// NAME and PREFIX are string literals (the FUT_APP_NAME / FUT_APP_PREFIX
// compile definitions); NS is the app namespace's reflection.
#define FUT_EXPORT_APP_NS(NAME, PREFIX, NS)                                       \
    extern "C" {                                                                  \
    FUT_EXPORT std::uint32_t fut_abi_version() { return ::fut::abi_version; }     \
    FUT_EXPORT const char* fut_app_name() { return (NAME); }                      \
    FUT_EXPORT const char* fut_app_prefix() { return (PREFIX); }                  \
    FUT_EXPORT void fut_app_register(::fut::fut_ctx* fut_ctx_ptr) {               \
        ::fut::scan::run<(NS)>(fut_ctx_ptr, (PREFIX));                            \
    }                                                                             \
    }
