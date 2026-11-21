#pragma once

// The executable's side of the module registry.
//
// Every module is stored with two keys: its interface type (the name from
// typeid) and a unique name. Lookup is by type by default and by name when
// several modules share a type:
//
//   modules.add("router", typeid(fut::http::router_module).name(), &router_module);
//   modules.add("primary", typeid(store).name(), &store_a);
//   modules.add("archive", typeid(store).name(), &store_b);
//
// typeid().name() is used rather than std::type_info because the two sides
// are different modules: comparing the mangled name as a string is stable
// across them, while comparing type_info objects is not guaranteed to be.
//
// Registration is a build-time-ish action: a duplicate name, or a name with
// no type, throws instead of silently shadowing.

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fut/ctx.hpp>

namespace fut::exec {

class module_registry {
public:
    struct entry {
        std::string name{};
        std::string type{};
        void* module = nullptr;
    };

    void add(std::string_view name, std::string_view type, void* module) {
        if (name.empty())
            throw std::runtime_error("a module must have a name");
        if (type.empty())
            throw std::runtime_error("module '" + std::string(name) + "' has no type");
        for (const entry& e : entries_)
            if (e.name == name)
                throw std::runtime_error("module '" + std::string(name) +
                                         "' is already registered");
        entries_.push_back(entry{std::string(name), std::string(type), module});
    }

    // name given: unique by construction, only the type must agree.
    // name empty: exactly one entry of that type, otherwise ambiguous.
    [[nodiscard]] fut::module_lookup find(std::string_view name,
                                          std::string_view type) const {
        if (!name.empty()) {
            for (const entry& e : entries_) {
                if (e.name != name) continue;
                if (!type.empty() && e.type != type)
                    return {nullptr, fut::module_status::wrong_type};
                return {e.module, fut::module_status::found};
            }
            return {nullptr, fut::module_status::not_found};
        }

        if (type.empty()) return {nullptr, fut::module_status::not_found};

        const entry* only = nullptr;
        for (const entry& e : entries_) {
            if (e.type != type) continue;
            if (only) return {nullptr, fut::module_status::ambiguous};
            only = &e;
        }
        if (!only) return {nullptr, fut::module_status::not_found};
        return {only->module, fut::module_status::found};
    }

    [[nodiscard]] const std::vector<entry>& entries() const { return entries_; }

private:
    std::vector<entry> entries_{};
};

} // namespace fut::exec
