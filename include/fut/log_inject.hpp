#pragma once

// fut/log_inject.hpp -- fut::log_group: a group reference for an app.
//
//   namespace admin {
//   [[=fut::log_group{}]] fut::log::group_ref log;              // "admin.log"
//   [[=fut::log_group{"admin.audit"}]] fut::log::group_ref audit;   // "admin.audit"
//   }
//
// The scanner fills it while the app registers: the name is the one given to
// the annotation, and only when none was given is it derived from the
// variable -- the app name plus the identifier, so two apps can both have a
// 'log' without colliding.
//
// The sink is the server's log service, so the app's lines flow through the
// same chains, formatters and files as the server's own. Nothing about logging
// is duplicated in the DLL -- group_ref is header-only and only calls the
// service pointer.
//
// Without a service (the executable publishes none) the variable is left
// empty and every call is a no-op, which is what a unit test wants.

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

#include <meta>

#include <fut/ctx.hpp>
#include <fut/detail/text.hpp>
#include <fut/log.hpp>
#include <fut/scannable.hpp>

namespace fut {

inline constexpr std::size_t log_group_name_max = 64;

struct log_group : scannable {
    detail::text_buf<log_group_name_max> name{};

    constexpr log_group() = default;
    constexpr log_group(std::string_view n) : name(n) {}

    template <std::meta::info Entity>
    void operation(fut::fut_ctx* ctx) const {
        static_assert(std::meta::is_variable(Entity),
                      "fut::log_group must annotate a namespace-scope variable");

        using Var = [: std::meta::type_of(Entity) :];
        static_assert(std::is_same_v<std::remove_cvref_t<Var>, fut::log::group_ref>,
                      "fut::log_group must annotate a fut::log::group_ref");

        std::string group{name.view()};
        if (group.empty()) {
            // No name given: "app.identifier", or just the identifier when
            // there is no app name (a unit test, an in-process registration).
            const std::string_view identifier = std::meta::identifier_of(Entity);
            const std::string_view app = ctx->app_name;
            group.reserve(app.size() + 1 + identifier.size());
            if (!app.empty()) {
                group.append(app);
                group.push_back('.');
            }
            group.append(identifier);
        }

        [: Entity :] = fut::log::group_ref{std::move(group),
                                          ctx->get_module<fut::log::service>()};
    }
};

} // namespace fut
