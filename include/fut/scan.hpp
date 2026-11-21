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
// The hook is found BY REFLECTION, not by a hard-coded call: the scanner
// reflects the annotation type's members, takes the one named `operation`
// and splices the call. Both shapes are accepted:
//
//     template <std::meta::info Entity>          // entity-aware hook
//     void operation(fut::fut_ctx*) const;
//
//     void operation(fut::fut_ctx*) const;      // entity-agnostic hook
//
// GCC reflects a member function TEMPLATE as is_template == true /
// is_function == false, which is how the two shapes are told apart. An
// annotation with neither gets an explanatory static_assert, not silence.
//
// The annotation value is the object: `self.*` is its configuration.
// Everything else it may need (the app prefix, server services) comes from
// the context, never from the scanner.
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
#include <type_traits>
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

namespace detail {

// A member function named `operation`, in either shape.
consteval bool is_hook(meta::info m) {
    if (!meta::has_identifier(m) || meta::identifier_of(m) != "operation") return false;
    try {
        if (meta::is_template(m)) return true;   // member function template
    } catch (...) {
    }
    return meta::is_function(m);                 // plain member function
}

consteval bool is_entity_hook(meta::info m) {
    try {
        return meta::is_template(m);
    } catch (...) {
        return false;
    }
}

// Used only for the diagnostic: an annotation without a hook would otherwise
// be a silent no-op.
template <class Ann>
consteval bool has_hook() {
    template for (constexpr meta::info m : std::define_static_array(
                      meta::members_of(^^Ann, meta::access_context::unchecked())))
        if (is_hook(m)) return true;
    return false;
}

template <meta::info Ann, meta::info Entity>
void call_hooks(fut_ctx* ctx) {
    constexpr auto self = [: meta::constant_of(Ann) :];
    using Self = std::remove_cvref_t<decltype(self)>;

    static_assert(has_hook<Self>(),
                  "fut: a scannable annotation needs an 'operation' hook -- either "
                  "'template <std::meta::info Entity> void operation(fut::fut_ctx*) const' "
                  "(entity-aware) or 'void operation(fut::fut_ctx*) const' "
                  "(entity-agnostic)");

    template for (constexpr meta::info m : std::define_static_array(
                      meta::members_of(^^Self, meta::access_context::unchecked()))) {
        if constexpr (is_hook(m)) {
            if constexpr (is_entity_hook(m))
                self.template [: m :]<Entity>(ctx);
            else
                self.[: m :](ctx);
        }
    }
}

} // namespace detail

// Every scannable annotation on one entity, never descending into it.
template <meta::info Entity>
void visit_entity(fut_ctx* ctx) {
    template for (constexpr meta::info ann :
                  std::define_static_array(scannable_annotations<Entity>()))
        detail::call_hooks<ann, Entity>(ctx);
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
