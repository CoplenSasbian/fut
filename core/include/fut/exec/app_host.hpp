#pragma once

// Hot reload: a server keeps serving while a new generation of apps is built,
// and the old one is unloaded only once the last request that started under it
// has finished.
//
//   app_host host{builder};       // builder makes one generation
//   auto gen = host.current();    // a request pins this for its lifetime
//   gen->rt.dispatch(req, out);
//
//   std::string problem;
//   if (auto next = host.reload(problem)) { /* new requests use it now */ }
//
// The invariants:
//   * a generation owns its app_loader, so its DLLs stay loaded as long as any
//     request (or anything else) holds a shared_ptr to it;
//   * reload builds the new generation completely before swapping, so a broken
//     DLL leaves the running server untouched;
//   * swapping is a pointer exchange under a mutex -- no request sees a half
//     built router.

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <fut/exec/app_loader.hpp>
#include <fut/exec/router.hpp>

#include <fut/export.hpp>

namespace fut::exec {

// One generation. Declaration order matters: the loader is destroyed last, so
// the router (which holds handlers built from DLL code) goes first.
struct app_generation {
    app_loader loader;
    router rt;
    std::vector<app_loader::entry> apps;
    std::size_t id = 0;
};

class FUT_API app_host {
public:
    using generation_ptr = std::shared_ptr<app_generation>;
    // Builds one generation from scratch; may throw.
    using builder = std::function<generation_ptr(std::size_t id)>;

    explicit app_host(builder build);

    // The generation a new request should use. Never null after the first
    // successful load.
    [[nodiscard]] generation_ptr current() const;

    // Builds the next generation and swaps it in. On failure the old one keeps
    // serving and out_error says why.
    generation_ptr reload(std::string& out_error);

    // How many generations have been installed (1 after the first load).
    [[nodiscard]] std::size_t generation() const;

private:
    builder build_;
    mutable std::mutex m_;
    generation_ptr current_{};
    std::size_t next_id_ = 1;
};

} // namespace fut::exec
