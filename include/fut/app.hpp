#pragma once

// fut/app.hpp -- the generic app module entry point.
//
// This is what a generated export unit includes: it knows that a module is
// scanned and that the executable calls one function, and nothing about
// what the module registers. HTTP, configuration, anything: the header does
// not care which annotations the app uses.
//
//   extern "C" FUT_EXPORT void fut_app_register(fut::fut_ctx* ctx) {
//       fut::scan::run<^^admin>(ctx, "/api/admin");
//   }
//
// FUT_EXPORT_APP_NS emits that function plus the three reporting exports:
//
//   fut_abi_version()            ABI check by the loader
//   fut_app_name() / _prefix()   logging and the status endpoint
//   fut_app_register(ctx)        the one function the server calls
//
// The app prefix (add_app's second argument) is baked in as the
// FUT_APP_PREFIX compile definition and handed to every scannable
// annotation through scan::run, so the module composes its own paths.

#include <cstdint>

#include <fut/ctx.hpp>
#include <fut/export.hpp>
#include <fut/scannable.hpp>
#include <fut/scan.hpp>

// NAME and PREFIX are string literals (FUT_APP_NAME / FUT_APP_PREFIX);
// NS is the app namespace's reflection.
#define FUT_EXPORT_APP_NS(NAME, PREFIX, NS)                                        \
    extern "C" {                                                                   \
    FUT_EXPORT std::uint32_t fut_abi_version() { return ::fut::abi_version; }      \
    FUT_EXPORT const char* fut_app_name() { return (NAME); }                       \
    FUT_EXPORT const char* fut_app_prefix() { return (PREFIX); }                   \
    FUT_EXPORT void fut_app_register(::fut::fut_ctx* fut_ctx_ptr) {                \
        ::fut::scan::run<(NS)>(fut_ctx_ptr, (PREFIX));                             \
    }                                                                              \
    }
