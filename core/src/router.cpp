#include <fut/exec/router.hpp>

#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <fut/http/error.hpp>
#include <fut/json.hpp>
#include <fut/log.hpp>

namespace fut::exec {

namespace {
constexpr auto router_log = fut::log::grp("router");
}
namespace {

namespace asio = boost::asio;

using terminal_fn = handler_fn;

// Runs chain[index..] around terminal. Each middleware receives a next_fn
// that resumes the rest of the chain, so a middleware can wrap, replace or
// short-circuit the response.
// Where the chain currently is. The next_fn below captures one pointer to it,
// which fits std::function's small-buffer optimization -- so walking the chain
// allocates nothing per request (a lambda capturing the three pieces directly
// would be 24 bytes and hit the heap).
struct chain_cursor {
    const std::vector<fut::http::middleware_fn>& chain;
    const terminal_fn& terminal;
    std::size_t index;
    fut::response* out;
};

asio::awaitable<void> run_chain(const std::vector<fut::http::middleware_fn>& chain,
                                std::size_t index, const terminal_fn& terminal,
                                const fut::request& req, const fut::path_match& params,
                                fut::response& out) {
    if (index >= chain.size()) co_return co_await terminal(req, params, out);

    chain_cursor cursor{chain, terminal, index, &out};
    fut::http::next_fn next = [&cursor](const fut::request& r, const fut::path_match& p,
                                        fut::response& o) -> asio::awaitable<void> {
        co_return co_await run_chain(cursor.chain, cursor.index + 1, cursor.terminal, r, p, o);
    };
    co_await chain[index](req, params, out, next);
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
            e.handler = [raw](const fut::request& req, const fut::path_match& params,
                              fut::response& out) -> asio::awaitable<void> {
                co_await raw(req, params, out);
            };
            continue;
        }

        e.handler = [chain = std::move(chain), raw](const fut::request& req,
                                                    const fut::path_match& params,
                                                    fut::response& out)
            -> asio::awaitable<void> {
            terminal_fn terminal = [raw](const fut::request& r, const fut::path_match& p,
                                         fut::response& o) -> asio::awaitable<void> {
                co_await raw(r, p, o);
            };
            co_await run_chain(chain, 0, terminal, req, params, out);
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

void router::make_error(fut::response& out, unsigned status, std::string_view message) const {
    if (out.started()) return;   // the head is out; nothing to change any more
    out = error_handler_ ? error_handler_(status, message) : fut::error(status, message);
}

namespace {

// The methods registered on a path, for Allow: HEAD and OPTIONS describe
// themselves even when nobody registered them.
std::string allowed_methods(const std::vector<fut::exec::router::entry>& entries,
                            std::string_view path) {
    std::string allow;
    auto add = [&allow](std::string_view name) {
        if (!allow.empty()) allow += ", ";
        allow += name;
    };
    bool head = false, options = false;
    for (const fut::exec::router::entry& e : entries) {
        fut::path_match params;
        if (!fut::match_pattern(e.pattern, path, params)) continue;
        if (e.method != fut::method::GET && e.method != fut::method::HEAD &&
            e.method != fut::method::OPTIONS)
            add(fut::to_string(e.method));
        head = head || e.method == fut::method::GET || e.method == fut::method::HEAD;
        options = true;
    }
    if (head) add("GET, HEAD");            // the GET route answers HEAD too
    if (options) add("OPTIONS");            // answered with this very header
    return allow;
}

} // namespace

asio::awaitable<void> router::dispatch_matched(const fut::request& req,
                                              fut::response& out) const {
    const auto m = fut::method_from_string(req.method);
    if (!m) co_return make_error(out, 405, "method not allowed");

    const auto found = match(*m, req.path);
    if (found) {
        // HEAD without a HEAD route is the GET route minus the body.
        if (*m == fut::method::HEAD) out.head_only();
        if (found->route->handler)
            co_return co_await found->route->handler(req, found->params, out);
        co_return co_await found->route->invoke(req, found->params, out);
    }

    // HEAD -> GET fallback (RFC 9110: HEAD is GET without the body).
    if (*m == fut::method::HEAD) {
        if (const auto via_get = match(fut::method::GET, req.path)) {
            out.head_only();
            if (via_get->route->handler)
                co_return co_await via_get->route->handler(req, via_get->params, out);
            co_return co_await via_get->route->invoke(req, via_get->params, out);
        }
    }

    const std::string allow = allowed_methods(entries_, req.path);

    // OPTIONS without a handler answers itself: what the path allows.
    if (*m == fut::method::OPTIONS) {
        if (allow.empty()) co_return make_error(out, 404, "no route");
        out.set_status(204).set_header("Allow", allow);
        co_return;
    }

    // 405 only when the path exists under another method; otherwise 404.
    if (!allow.empty()) {
        // 405 after the body exists: make_error replaces the response, so the
        // Allow header goes on afterwards.
        make_error(out, 405, "method not allowed");
        if (!out.started()) out.set_header("Allow", allow);
        co_return;
    }
    co_return make_error(out, 404, "no route");
}

asio::awaitable<void> router::dispatch_chain(const fut::request& req,
                                            fut::response& out) const {
    if (global_.empty()) co_return co_await dispatch_matched(req, out);

    // Platform middleware sees every request, including the ones that will
    // not match anything, so it gets no path parameters.
    terminal_fn terminal = [this](const fut::request& r, const fut::path_match&,
                                  fut::response& o) -> asio::awaitable<void> {
        co_return co_await dispatch_matched(r, o);
    };
    const fut::path_match none{};
    co_return co_await run_chain(global_, 0, terminal, req, none, out);
}

// The one place where errors become responses: an action, a middleware or a
// binding may throw; every app therefore reports failures the same way.
asio::awaitable<void> router::dispatch(const fut::request& req, fut::response& out) const {
    try {
        co_await dispatch_chain(req, out);
    } catch (const fut::stream_error&) {
        // The client went away mid-stream: nothing left to answer.
    } catch (const fut::http_error& e) {
        make_error(out, e.status(), e.what());
    } catch (const fut::bind_error& e) {
        make_error(out, 400, e.what());
    } catch (const std::exception& e) {
        router_log.error("unhandled exception on {} {}: {}", std::string(req.method),
                         std::string(req.target), e.what());
        make_error(out, 500, "internal server error");
    } catch (...) {
        router_log.error("unhandled non-standard exception on {} {}", std::string(req.method),
                         std::string(req.target));
        make_error(out, 500, "internal server error");
    }
    co_return;
}

} // namespace fut::exec
