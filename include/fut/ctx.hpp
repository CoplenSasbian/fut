#pragma once

// fut/ctx.hpp -- the context handed to an app module while it registers.
//
// The server executable calls the DLL's exported fut_app_register(fut_ctx*).
// Through the context the app reaches server services ("modules") without
// linking against the executable:
//
//     extern "C" FUT_EXPORT void fut_app_register(fut::fut_ctx* ctx) {
//         auto* router = ctx->get_module<fut::http::router_module>("router");
//         ...
//     }
//
// A module is registered under two keys: its interface type (typeid) and a
// name. The usual lookup is by type --
//
//     auto* router = ctx->get_module<fut::http::router_module>();   // type only
//
// and the name is only needed when two modules share a type:
//
//     auto* primary = ctx->get_module<store>("primary");
//     auto* archive = ctx->get_module<store>("archive");
//
// Asking for a type that several modules answer without giving a name is an
// error (fut::module_error), not a silent pick: an ambiguous module is a
// configuration bug, and picking one arbitrarily hides it.
//
// Nothing about the modules is known here -- this header is the whole
// contract between the executable and the libraries it loads. Both sides must
// be built from these headers with the same compiler.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>

namespace fut {

struct fut_ctx;

// Result of one lookup. The status is an integer on purpose: the lookup runs
// on the executable's side, so the DLL decides whether to throw.
enum class module_status : std::uint8_t {
    found = 0,
    not_found = 1,
    ambiguous = 2,    // several modules of that type, no name given
    wrong_type = 3,   // that name exists, but it is a different type
};

struct module_lookup {
    void* module = nullptr;
    module_status status = module_status::not_found;
};

// Language-neutral on purpose: std::string_view for the keys, a POD result.
using module_lookup_fn = module_lookup (*)(fut_ctx*, std::string_view name,
                                           std::string_view type);

// Thrown by the typed accessors for an ambiguous or mismatched request.
class module_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct fut_ctx {
    // Server-private; apps must not touch it.
    void* host = nullptr;

    module_lookup_fn get_module_fn = nullptr;

    // Identity of the app currently registering (set by the server).
    std::string_view app_name{};
    std::string_view app_prefix{};

    // The raw lookup: name and/or type, plus the status. Callers that can
    // recover from "ambiguous" (fut::inject falls back to the variable's name)
    // use this instead of the throwing accessor below.
    [[nodiscard]] module_lookup find_module(std::string_view name,
                                            std::string_view type) const {
        if (!get_module_fn) return {nullptr, module_status::not_found};
        return get_module_fn(const_cast<fut_ctx*>(this), name, type);
    }

    // Untyped lookup by name; nullptr when there is no such module.
    [[nodiscard]] void* get_module(std::string_view name) const {
        return find_module(name, {}).module;
    }

    // Typed lookup. With a name the module must also have that type; without
    // one, exactly one module of the type must exist. Throws module_error
    // otherwise; returns nullptr when nothing is registered under it.
    template <class T>
    [[nodiscard]] T* get_module(std::string_view name = {}) const {
        const auto result = find_module(name, typeid(T).name());
        switch (result.status) {
        case module_status::found:
            return static_cast<T*>(result.module);
        case module_status::not_found:
            return nullptr;
        case module_status::ambiguous:
            throw module_error(std::string{"ambiguous module lookup: several modules are "
                                           "registered as '"} +
                               typeid(T).name() + "'; ask for one by name");
        case module_status::wrong_type:
            throw module_error("module '" + std::string(name) + "' is not a '" +
                               std::string(typeid(T).name()) + "'");
        }
        return nullptr;
    }
};

// Convenience wrapper kept for readability at call sites.
template <class T>
[[nodiscard]] T* module_as(const fut_ctx* ctx, std::string_view name = {}) {
    return ctx ? ctx->get_module<T>(name) : nullptr;
}

} // namespace fut
