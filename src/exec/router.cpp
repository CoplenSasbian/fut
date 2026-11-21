#include "router.hpp"

#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <fut/json.hpp>

namespace fut::exec {
namespace {

namespace asio = boost::asio;

using terminal_fn = handler_fn;

// Runs chain[index..] around terminal. Each middleware receives a next_fn
// that resumes the rest of the chain, so a middleware can wrap, replace or
// short-circuit the response.
asio::awaitable<fut::response> run_chain(const std::vector<fut::http::middleware_fn>& chain,
                                         std::size_t index, const terminal_fn& terminal,
                                         const fut::request& req,
                                         const fut::path_match& params) {
    if (index >= chain.size()) co_return co_await terminal(req, params);

    fut::http::next_fn next = [&chain, index, &terminal](
                                  const fut::request& r,
                                  const fut::path_match& p) -> asio::awaitable<fut::response> {
        co_return co_await run_chain(chain, index + 1, terminal, r, p);
    };
    co_return co_await chain[index](req, params, next);
}

} // namespace

void router::add_route(fut::method m, std::string_view pattern, fut::http::route_fn fn) {
    entry e;
    e.method = m;
    e.pattern.assign(pattern);
    e.invoke = fn;
    if (current_app_ < apps_.size()) {
        e.app = apps_[current_app_].name;
        ++apps_[current_app_].routes;
    }
    entries_.push_back(std::move(e));
}

void router::add_middleware(fut::http::middleware_hook fn) {
    if (current_app_ < apps_.size()) {
        app_chains_[apps_[current_app_].name].push_back(fn);
    } else {
        // Registered outside an app window: treat it as platform middleware.
        global_.push_back(fn);
    }
}

void router::add_global_middleware(fut::http::middleware_fn fn) {
    global_.push_back(std::move(fn));
}

void router::add_route_thunk(void* self, fut::method m, std::string_view pattern,
                             fut::http::route_fn fn) {
    static_cast<router*>(self)->add_route(m, pattern, fn);
}

void router::add_middleware_thunk(void* self, fut::http::middleware_hook fn) {
    static_cast<router*>(self)->add_middleware(fn);
}

void router::begin_app(std::string_view name, std::string_view prefix) {
    apps_.push_back(app_info{std::string(name), std::string(prefix), 0});
    current_app_ = apps_.size() - 1;
}

void router::end_app() { current_app_ = npos; }

void router::finalize() {
    for (entry& e : entries_) {
        // Wrap the hooks here: these std::functions belong to the executable,
        // so they stay destructible even after the app DLL is unloaded.
        std::vector<fut::http::middleware_fn> chain;
        const auto it = app_chains_.find(e.app);
        if (it != app_chains_.end())
            for (const fut::http::middleware_hook hook : it->second)
                chain.emplace_back(hook);

        const fut::http::route_fn raw = e.invoke;
        if (chain.empty()) {
            e.handler = [raw](const fut::request& req,
                              const fut::path_match& params) -> asio::awaitable<fut::response> {
                co_return co_await raw(req, params);
            };
            continue;
        }

        e.handler = [chain = std::move(chain), raw](const fut::request& req,
                                                    const fut::path_match& params)
            -> asio::awaitable<fut::response> {
            terminal_fn terminal = [raw](const fut::request& r,
                                         const fut::path_match& p) -> asio::awaitable<fut::response> {
                co_return co_await raw(r, p);
            };
            co_return co_await run_chain(chain, 0, terminal, req, params);
        };
    }
}

std::optional<router::hit> router::match(fut::method m, std::string_view path) const {
    const entry* best = nullptr;
    fut::path_match best_params{};
    std::size_t best_score = 0;
    for (const entry& e : entries_) {
        if (e.method != m) continue;
        fut::path_match params;
        if (!fut::match_pattern(e.pattern, path, params)) continue;
        const std::size_t score = fut::literal_segments(e.pattern);
        if (!best || score > best_score) {
            best = &e;
            best_params = params;
            best_score = score;
        }
    }
    if (!best) return std::nullopt;
    return hit{best, best_params};
}

asio::awaitable<fut::response> router::dispatch_matched(const fut::request& req) const {
    const auto m = fut::method_from_string(req.method);
    if (!m) co_return fut::error(405, "method not allowed");

    const auto found = match(*m, req.path);
    if (!found) co_return fut::error(404, "no route");

    try {
        if (found->route->handler)
            co_return co_await found->route->handler(req, found->params);
        co_return co_await found->route->invoke(req, found->params);
    } catch (const fut::bind_error& e) {
        co_return fut::error(400, e.what());
    } catch (const std::exception& e) {
        co_return fut::error(500, e.what());
    }
}

asio::awaitable<fut::response> router::dispatch(const fut::request& req) const {
    if (global_.empty()) co_return co_await dispatch_matched(req);

    // Platform middleware sees every request, including the ones that will
    // not match anything, so it gets no path parameters.
    terminal_fn terminal = [this](const fut::request& r,
                                  const fut::path_match&) -> asio::awaitable<fut::response> {
        co_return co_await dispatch_matched(r);
    };
    const fut::path_match none{};
    co_return co_await run_chain(global_, 0, terminal, req, none);
}

} // namespace fut::exec
