#pragma once

// Part of the /api/web app: the "beta" route group.
//
// add_app(web ...) unity-includes every listed source into one generated
// translation unit, and the recursive namespace scan picks these up exactly
// like the flat actions in web_app.hpp -- an app may split its actions
// across as many files as it likes, with no registration and no lists.

#include <string>

#include <fut/annotate.hpp>
#include <fut/refl_router.hpp>

namespace asio = boost::asio;

namespace web {
namespace beta {

[[=fut::http_get("/beta/note")]]
inline asio::awaitable<std::string> beta_note() {
    co_return "from namespace web::beta";
}

} // namespace beta
} // namespace web
