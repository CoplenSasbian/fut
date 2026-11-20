#pragma once

// fut/http/http.hpp -- the single include for the fut HTTP library.
//
//   #include <fut/http/http.hpp>
//
// brings in the entire annotation-routing stack:
//   route.hpp       request/response, method enum, scannable, app_table
//   bind.hpp        from_body/from_query/from_path/from_header
//   invoke.hpp      parameter binding, controller invoke, return adaptation
//   annotate.hpp    http_action + constexpr verbs + controller(scannable)
//   refl_router.hpp the generic namespace scanner + FUT_EXPORT_APP_NS
//
// JSON helpers (fut::json / fut::parse) and URL helpers are separate:
//   #include <fut/json.hpp>
//   #include <fut/url.hpp>

#include <fut/http/route.hpp>
#include <fut/http/bind.hpp>
#include <fut/http/invoke.hpp>
#include <fut/http/annotate.hpp>
#include <fut/http/refl_router.hpp>
