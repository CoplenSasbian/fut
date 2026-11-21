#pragma once

// fut/inject.hpp -- fill a namespace-scope pointer from the context.
//
// An app sometimes needs one of the server's objects rather than a
// per-request value: the io_context, a cache, a database pool. Marking a
// namespace variable with fut::inject makes the scanner assign it while the
// app registers:
//
//     namespace admin {
//     [[=fut::inject{}]] boost::asio::io_context* g_io = nullptr;
//     }
//
// The type of the variable decides what is looked up: the pointee type is the
// module key, so the line above fetches the module registered as
// boost::asio::io_context. Pointers only, deliberately -- a reference has to
// be bound where it is declared, which is exactly the moment the scanner does
// not control.
//
// The injected object outlives the app DLL (the server owns it), and a module
// that is not published (or is ambiguous under its type) makes registration
// fail loudly instead of leaving a null pointer behind.

#include <cstddef>
#include <type_traits>

#include <meta>

#include <fut/ctx.hpp>
#include <fut/scannable.hpp>

namespace fut {

inline constexpr std::size_t inject_max = 1;

struct inject : scannable {
    template <std::meta::info Entity>
    void operation(fut::fut_ctx* ctx) const {
        static_assert(std::meta::is_variable(Entity),
                      "fut::inject must annotate a namespace-scope variable");

        using Var = [: std::meta::type_of(Entity) :];
        using Bare = std::remove_cvref_t<Var>;
        static_assert(std::is_pointer_v<Bare>,
                      "fut::inject needs a pointer variable, e.g. "
                      "'boost::asio::io_context* g_io = nullptr'");

        using Target = std::remove_cv_t<std::remove_pointer_t<Bare>>;
        [: Entity :] = ctx->get_module<Target>();
    }
};

} // namespace fut
