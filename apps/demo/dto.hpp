#pragma once

// The app's data shapes. Plain aggregates on purpose: glaze serialises them
// member by member, and the same struct serves as an input and an output.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace demo {

struct pong {
    bool pong{};
    std::string app{};   // reused as a free-form string in a few routes
};

struct greeting_in {
    std::string message{};
    int count{0};
};

struct greeting_out {
    std::string message{};
    int doubled{};
    long long sum{};
};

struct query_in {   // a whole DTO from the query string
    std::string name{};
    int count{1};
    bool verbose{false};
};

struct item {
    long long id{};
    std::string name{};
    int quantity{};
};

struct request_out {
    std::string method{};
    std::string path{};
    std::string query{};
    std::size_t headers{};
};

struct upload_out {
    std::string title{};
    std::string filename{};
    std::size_t bytes{};
    std::string content_type{};
};

struct io_out {
    bool injected{};
    bool timer_fired{};
};

struct log_out {
    bool logged{};
    std::string group{};
};

struct route_entry {
    std::string method{};
    std::string path{};
};

struct info_out {
    std::string name{};
    std::string prefix{};
    unsigned abi{};
    std::vector<route_entry> routes{};
};

// Defined in routes.hpp, once every controller is declared. The /info route
// reflects over the controllers instead of keeping a hand-written list.
std::vector<route_entry> all_routes();

} // namespace demo
