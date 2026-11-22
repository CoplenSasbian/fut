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
// boost::asio::io_context -- no name needed while that type is unique.
//
// When several modules share the type, the name picks one:
//
//     [[=fut::inject{"primary"}]] store* a = nullptr;   // by the given name
//     [[=fut::inject{}]] store* archive = nullptr;      // by the variable's name
//
// The fallback is deliberate: a variable called 'archive' asks for the module
// named "archive" before giving up, so the common case needs no string while
// the ambiguous one still has an answer. An ambiguous type with neither name is
// an error (fut::module_error), never an arbitrary pick.
//
// Pointers only, deliberately -- a reference has to be bound where it is
// declared, which is exactly the moment the scanner does not control.

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

#include <meta>

#include <fut/ctx.hpp>
#include <fut/detail/text.hpp>
#include <fut/scannable.hpp>

namespace fut {

inline constexpr std::size_t inject_name_max = 64;

struct inject : scannable {
    detail::text_buf<inject_name_max> name{};

    constexpr inject() = default;
    constexpr inject(std::string_view n) : name(n) {}

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
        const std::string_view type = typeid(Target).name();

        // 1) the type alone. A unique module of that type needs no name.
        auto found = ctx->find_module({}, type);

        // 2) several of them: the annotation's name, else the variable's name.
        if (found.status == module_status::ambiguous) {
            const std::string_view chosen =
                name.size() != 0 ? name.view() : std::meta::identifier_of(Entity);
            found = ctx->find_module(chosen, type);
            if (found.status == module_status::wrong_type) {
                throw module_error("module '" + std::string(chosen) + "' is not a '" +
                                   std::string(type) + "'");
            }
            if (found.status == module_status::not_found) {
                throw module_error("ambiguous module lookup: several modules are registered as '" +
                                   std::string(type) + "' and none is named '" +
                                   std::string(chosen) +
                                   "'; give the annotation a name or rename the variable");
            }
        }

        [: Entity :] = static_cast<Target*>(found.module);
    }
};

} // namespace fut
