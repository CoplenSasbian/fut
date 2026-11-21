#pragma once

// fut/scan.hpp -- the generic namespace scanner.
//
//     fut::scan::run<^^admin>(ctx);
//
// Recursion rule (exactly one level, namespaces only):
//
//   * the scanner looks at the DIRECT members of the namespace;
//   * a member that is a namespace is the ONLY thing it recurses into;
//   * any other member (class, function, variable, enum, ...) is never
//     descended into -- every scannable annotation on it is called once.
//
// So a controller class nested inside another class is invisible to the
// scan, while a controller in a nested namespace is found.
//
// Each annotation is called as an object:
//
//     constexpr auto self = [: std::meta::constant_of(annotation) :];
//     self.template operation<Entity>(ctx)
//
// The annotation's own members are its configuration (`this`); everything
// else it may need (the app prefix, server services) comes from the
// context, never from the scanner.
//
// Everything here is a `template for` expansion over reflection ranges: no
// fixed-size caps, no index packs, no per-index helper templates. Reflection
// ranges are `std::vector`s built in constant evaluation and turned into
// static arrays by `std::define_static_array` -- nothing escapes compile
// time, so no size has to be guessed anywhere.
//
// Nothing here knows about HTTP: a new scannable annotation needs no change
// to this file.

#include <meta>
#include <vector>

#include <fut/scannable.hpp>

namespace fut::scan {

namespace meta = std::meta;

// The scannable annotations on one entity.
//
// A plain std::vector is fine: the whole thing never leaves constant
// evaluation -- std::define_static_array turns the result into a static
// range before any code is generated, so there is no fixed cap to truncate
// and no array to size by hand.
//
// Some namespace members cannot be queried (an unresolved overload set is
// one): annotations_of throws for those. They carry no annotation, so
// skipping them is correct -- the catch is the guard, not an error path.
template <meta::info Entity>
consteval std::vector<meta::info> scannable_annotations() {
    std::vector<meta::info> out;
    try {
        for (auto a : meta::annotations_of(Entity))
            if (meta::is_base_of_type(^^fut::scannable, meta::type_of(a)))
                out.push_back(a);
    } catch (...) {
    }
    return out;
}

// Every scannable annotation on one entity, never descending into it.
template <meta::info Entity>
void visit_entity(fut_ctx* ctx) {
    template for (constexpr meta::info a :
                  std::define_static_array(scannable_annotations<Entity>())) {
        constexpr auto self = [: meta::constant_of(a) :];
        self.template operation<Entity>(ctx);
    }
}

template <meta::info Ns>
void run(fut_ctx* ctx) {
    template for (constexpr meta::info m :
                  std::define_static_array(
                      meta::members_of(Ns, meta::access_context::unchecked()))) {
        if constexpr (meta::is_namespace(m))
            run<m>(ctx);              // the only recursion
        else
            visit_entity<m>(ctx);     // handed over, never descended
    }
}

} // namespace fut::scan
