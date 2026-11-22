#pragma once

// fut/annotations.hpp -- generic annotation queries shared by every module.
//
// Annotations are metadata on an entity. "Which controller annotation is on
// this class?", "which verbs are on this method?", "does this class ask for
// the health endpoint?" are the same reflection query everywhere, so it lives
// here once.
//
// The helpers are cv-agnostic on purpose: GCC records a constexpr-function
// annotation value as cv-qualified (^^const http_action) and a struct
// annotation as plain (controller). Callers should not have to know which.

#include <meta>
#include <vector>

namespace fut::ann {

namespace meta = std::meta;

// True when `annotation` is a value of type T, ignoring cv-qualifiers.
consteval bool is_of(std::meta::info annotation, std::meta::info wanted) {
    return meta::remove_cvref(meta::type_of(annotation)) == meta::dealias(wanted);
}

// All annotations of type T on an entity or a member, in declaration order.
//
// Some infos cannot be queried (an unresolved overload set, for instance):
// they carry no annotation, so skipping them is correct -- the catch is the
// guard, not an error path.
template <class T>
consteval std::vector<meta::info> of(std::meta::info entity) {
    std::vector<meta::info> out;
    try {
        for (auto a : meta::annotations_of(entity))
            if (is_of(a, ^^T)) out.push_back(a);
    } catch (...) {
    }
    return out;
}

// The first annotation of type T, or an empty info when there is none.
template <class T>
consteval std::meta::info first(std::meta::info entity) {
    for (auto a : of<T>(entity)) return a;
    return meta::info{};
}

template <class T>
consteval bool has(std::meta::info entity) {
    return first<T>(entity) != meta::info{};
}

} // namespace fut::ann
