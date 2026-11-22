#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "http_parser.hpp"

using fut::exec::request_parser;

namespace {

// Feeds one whole buffer, expecting exactly one complete message, and
// returns the parsed message (the parser itself is not copyable).
fut::exec::http_request_message parse_one(std::string_view data,
                                          std::size_t* consumed = nullptr) {
    request_parser parser;
    const auto status = parser.feed(data.data(), data.size());
    REQUIRE(status == request_parser::status::complete);
    if (consumed) *consumed = parser.consumed();
    return parser.message();
}

} // namespace

TEST_SUITE("http parser") {

TEST_CASE("a plain GET") {
    const auto m = parse_one(
        "GET /api/admin/ping HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Accept: */*\r\n\r\n");
    CHECK(m.method == "GET");
    CHECK(m.target == "/api/admin/ping");
    CHECK(m.version == "HTTP/1.1");
    REQUIRE(m.headers.size() == 2);
    CHECK(m.headers[0].first == "Host");
    CHECK(m.headers[0].second == "localhost");
    CHECK(m.body.empty());
    CHECK(m.keep_alive == true);
}

TEST_CASE("POST body is read up to Content-Length") {
    const auto m = parse_one(
        "POST /add HTTP/1.1\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n\r\n"
        "{\"a\":2,\"b\":5}");
    CHECK(m.method == "POST");
    CHECK(m.body == R"({"a":2,"b":5})");
}

TEST_CASE("chunked body is reassembled by the parser") {
    const auto m = parse_one(
        "POST /add HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "5\r\n{\"a\":\r\n"
        "4\r\n2,\"b\r\n"
        "4\r\n\":5}\r\n"
        "0\r\n\r\n");
    CHECK(m.body == R"({"a":2,"b":5})");
}

TEST_CASE("a split delivery still parses") {
    request_parser parser;
    const std::string_view part1 = "GET /x HTTP/1.1\r\nHost: h\r\n";
    CHECK(parser.feed(part1.data(), part1.size()) == request_parser::status::need_more);

    const std::string_view part2 = "\r\n";
    CHECK(parser.feed(part2.data(), part2.size()) == request_parser::status::complete);
    CHECK(parser.message().target == "/x");
}

TEST_CASE("pipelined requests report their own length") {
    const std::string_view two =
        "GET /one HTTP/1.1\r\nHost: h\r\n\r\n"
        "GET /two HTTP/1.1\r\nHost: h\r\n\r\n";

    request_parser parser;
    REQUIRE(parser.feed(two.data(), two.size()) == request_parser::status::complete);
    CHECK(parser.message().target == "/one");
    const std::size_t first = parser.consumed();
    CHECK(first == std::string_view("GET /one HTTP/1.1\r\nHost: h\r\n\r\n").size());

    parser.reset();
    const std::string_view rest = two.substr(first);
    REQUIRE(parser.feed(rest.data(), rest.size()) == request_parser::status::complete);
    CHECK(parser.message().target == "/two");
}

TEST_CASE("keep-alive follows the protocol rules") {
    SUBCASE("HTTP/1.1 defaults to keep-alive") {
        CHECK(parse_one("GET / HTTP/1.1\r\nHost: h\r\n\r\n").keep_alive);
    }
    SUBCASE("HTTP/1.0 defaults to close") {
        CHECK_FALSE(parse_one("GET / HTTP/1.0\r\nHost: h\r\n\r\n").keep_alive);
    }
    SUBCASE("Connection: close wins") {
        CHECK_FALSE(parse_one("GET / HTTP/1.1\r\nHost: h\r\nConnection: close\r\n\r\n")
                        .keep_alive);
    }
    SUBCASE("Connection: keep-alive upgrades HTTP/1.0") {
        CHECK(parse_one("GET / HTTP/1.0\r\nHost: h\r\nConnection: keep-alive\r\n\r\n")
                  .keep_alive);
    }
}

TEST_CASE("garbage is rejected") {
    request_parser parser;
    const std::string_view bad = "not a request\r\n\r\n";
    CHECK(parser.feed(bad.data(), bad.size()) == request_parser::status::error);
    CHECK_FALSE(parser.error().empty());
}

TEST_CASE("header lookup is case-insensitive") {
    const auto m = parse_one("GET / HTTP/1.1\r\nHost: h\r\nX-Fut: yes\r\n\r\n");
    CHECK(m.header("x-fut") == "yes");
    CHECK(m.header("HOST") == "h");
    CHECK(m.header("missing").empty());
}

TEST_CASE("response serialization writes the framing headers") {
    const std::string ok = fut::exec::serialize_response(
        200, "application/json", {{"X-Test", "1"}}, R"({"ok":true})", true);
    CHECK(ok.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(ok.find("Content-Type: application/json\r\n") != std::string::npos);
    CHECK(ok.find("Content-Length: 11\r\n") != std::string::npos);
    CHECK(ok.find("Connection: keep-alive\r\n") != std::string::npos);
    CHECK(ok.find("X-Test: 1\r\n") != std::string::npos);
    CHECK(ok.ends_with(R"({"ok":true})"));

    const std::string not_found =
        fut::exec::serialize_response(404, "application/json", {}, "{}", false);
    CHECK(not_found.rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0);
    CHECK(not_found.find("Connection: close\r\n") != std::string::npos);
}

} // TEST_SUITE

TEST_CASE("Expect: 100-continue stops at the headers and resumes") {
    fut::exec::request_parser p;

    // headers only: the parser pauses before the body
    const std::string head =
        "POST /x HTTP/1.1\r\nHost: h\r\nContent-Type: application/json\r\n"
        "Expect: 100-continue\r\nContent-Length: 7\r\n\r\n";
    CHECK(p.feed(head.data(), head.size()) == fut::exec::request_parser::status::interim);
    CHECK(p.message().method == "POST");
    CHECK(p.message().expect_continue);
    CHECK(p.consumed() == head.size());

    // the body follows once the caller has answered
    p.resume();
    const std::string body = "{\"a\":1}";
    CHECK(p.feed(body.data(), body.size()) == fut::exec::request_parser::status::complete);
    CHECK(p.message().body == body);
}

TEST_CASE("a header bomb or an oversized target is reported, not buffered") {
    fut::exec::request_parser p;
    std::string bomb = "GET /x HTTP/1.1\r\nHost: h\r\n";
    for (int i = 0; i < 400; ++i) bomb += "X-Pad-" + std::to_string(i) + ": " + std::string(200, 'a') + "\r\n";
    bomb += "\r\n";
    CHECK(p.feed(bomb.data(), bomb.size()) == fut::exec::request_parser::status::interim);
    CHECK(p.message().headers_too_large);
    CHECK(p.message().method == "GET");   // still filled for the 431 response

    fut::exec::request_parser q;
    std::string long_target = "GET /" + std::string(9000, 'a') + " HTTP/1.1\r\nHost: h\r\n\r\n";
    CHECK(q.feed(long_target.data(), long_target.size()) ==
          fut::exec::request_parser::status::interim);
    CHECK(q.message().target_too_large);
}
