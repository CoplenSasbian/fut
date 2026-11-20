#pragma once

// fut/ctx.hpp -- the context handed to an app module while it registers.
//
// The server executable calls the DLL's exported fut_app_register(fut_ctx*).
// Through the context the app reaches server services ("modules") without
// linking against the executable:
//
//     extern "C" FUT_EXPORT void fut_app_register(fut::fut_ctx* ctx) {
//         auto* router = fut::module_as<fut::http::router_module>(ctx, "router");
//         ...
//     }
//
// get_module() returns an untyped pointer. The caller casts it to the type
// agreed for that module name; casting to any other type is undefined
// behaviour. No module is known here -- this header is the whole contract
// between the executable and the libraries it loads.

#include <string_view>

namespace fut {

struct fut_ctx {
    // Server-private; apps must not touch it.
    void* host = nullptr;

    // Module lookup: the interface registered under `name`, or nullptr.
    void* (*get_module_fn)(fut_ctx*, std::string_view) = nullptr;

    [[nodiscard]] void* get_module(std::string_view name) const {
        return get_module_fn ? get_module_fn(const_cast<fut_ctx*>(this), name) : nullptr;
    }
};

// Typed lookup: the cast is the caller's promise about the module's type.
template <class T>
[[nodiscard]] T* module_as(const fut_ctx* ctx, std::string_view name) {
    return static_cast<T*>(ctx ? ctx->get_module(name) : nullptr);
}

} // namespace fut
