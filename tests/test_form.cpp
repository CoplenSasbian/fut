#include <fut/json.hpp>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fut/http/form.hpp>

namespace {

fut::request form_request(std::string_view content_type, std::string_view body) {
    static std::vector<std::pair<std::string_view, std::string_view>> headers;
    headers.clear();
    if (!content_type.empty()) headers.emplace_back("Content-Type", content_type);
    fut::request req{};
    req.method = "POST";
    req.headers = headers;
    req.body = body;
    return req;
}

// Builds a multipart body the way a browser does: CRLF everywhere, an empty
// line between the part headers and the data.
std::string multipart(std::string_view boundary) {
    std::string b;
    const std::string d = "--" + std::string(boundary) + "\r\n";

    b += d;
    b += "Content-Disposition: form-data; name=\"title\"\r\n\r\n";
    b += "hello form\r\n";

    b += d;
    b += "Content-Disposition: form-data; name=\"count\"\r\n\r\n";
    b += "42\r\n";

    b += d;
    b += "Content-Disposition: form-data; name=\"file\"; filename=\"notes.txt\"\r\n";
    b += "Content-Type: text/plain\r\n\r\n";
    b += "line one\nline two\r\n";

    b += "--" + std::string(boundary) + "--\r\n";
    return b;
}

} // namespace

TEST_SUITE("form parsing") {

TEST_CASE("urlencoded fields are decoded") {
    fut::form_data f{};
    fut::parse_urlencoded("name=hello+world&n=42&city=%E5%8C%97%E4%BA%AC", f);

    REQUIRE(f.fields.size() == 3);
    CHECK(*f.field("name") == "hello world");
    CHECK(*f.field("n") == "42");
    CHECK(*f.field("city") == "\xE5\x8C\x97\xE4\xBA\xAC");   // 北京 in UTF-8
    CHECK(f.field("missing") == nullptr);
    CHECK(f.files.empty());
}

TEST_CASE("multipart fields and a file are split correctly") {
    fut::form_data f{};
    std::string why;
    const auto body = multipart("XyZ");
    REQUIRE(fut::parse_multipart(body, "multipart/form-data; boundary=XyZ", f, why));

    REQUIRE(f.fields.size() == 2);
    CHECK(*f.field("title") == "hello form");
    CHECK(*f.field("count") == "42");

    REQUIRE(f.files.size() == 1);
    const fut::uploaded_file* file = f.file("file");
    REQUIRE(file != nullptr);
    CHECK(file->filename == "notes.txt");
    CHECK(file->content_type == "text/plain");
    CHECK(file->data == "line one\nline two");
    CHECK(file->size() == file->data.size());
}

TEST_CASE("a quoted boundary is accepted") {
    fut::form_data f{};
    std::string why;
    const auto body = multipart("a b");
    REQUIRE(fut::parse_multipart(body, "multipart/form-data; boundary=\"a b\"", f, why));
    CHECK(f.fields.size() == 2);
}

TEST_CASE("an empty file part is still a file") {
    const std::string body =
        "--b\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"empty.bin\"\r\n\r\n"
        "\r\n"
        "--b--\r\n";
    fut::form_data f{};
    std::string why;
    REQUIRE(fut::parse_multipart(body, "multipart/form-data; boundary=b", f, why));
    REQUIRE(f.files.size() == 1);
    CHECK(f.files[0].filename == "empty.bin");
    CHECK(f.files[0].empty());
}

TEST_CASE("malformed multipart is reported, not silently dropped") {
    fut::form_data f{};
    std::string why;

    CHECK_FALSE(fut::parse_multipart("irrelevant", "multipart/form-data", f, why));
    CHECK(why.find("boundary") != std::string::npos);

    why.clear();
    CHECK_FALSE(fut::parse_multipart("--b\r\nno header terminator",
                                     "multipart/form-data; boundary=b", f, why));
    CHECK_FALSE(why.empty());
}

TEST_CASE("parse_form dispatches on Content-Type") {
    const fut::request urlencoded = form_request(
        "application/x-www-form-urlencoded; charset=utf-8", "a=1&b=2");
    const auto uf = fut::parse_form(urlencoded);
    CHECK(*uf.field("a") == "1");

    const auto body = multipart("qq");
    const fut::request multipart_req = form_request("multipart/form-data; boundary=qq", body);
    const auto mf = fut::parse_form(multipart_req);
    CHECK(mf.files.size() == 1);

    const fut::request json = form_request("application/json", R"({"a":1})");
    CHECK_THROWS_AS((void)fut::parse_form(json), fut::bind_error);
}

TEST_CASE("request header lookup ignores case") {
    const fut::request req =
        form_request("application/x-www-form-urlencoded", "x=1");
    CHECK(req.header("content-type").rfind("application/x-www-form-urlencoded", 0) == 0);
    CHECK(req.header("CONTENT-TYPE") == req.header("Content-Type"));
    CHECK(req.header("missing").empty());
}

} // TEST_SUITE

TEST_CASE("percent decoding is byte-wise and leaves bad escapes alone") {
    // UTF-8 percent-encoded: "你" is E4 BD A0
    const auto pairs = fut::parse_query("name=%E4%BD%A0&plus=a+b&bad=%ZZ&half=%4");
    REQUIRE(pairs.size() == 4);
    CHECK(pairs[0].second == "\xE4\xBD\xA0");
    CHECK(pairs[1].second == "a b");          // '+' is a space in a query
    CHECK(pairs[2].second == "%ZZ");          // not a valid escape: kept as is
    CHECK(pairs[3].second == "%4");

    // no charset assumption anywhere: the bytes are what they are
    CHECK(fut::url_decode("%41%42") == "AB");
}

TEST_CASE("a multipart filename* is decoded as UTF-8") {
    std::string body = "--x\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"raw.txt\"; "
            "filename*=UTF-8''%E4%BD%A0.txt\r\n";
    body += "Content-Type: text/plain\r\n\r\n";
    body += "data\r\n--x--\r\n";

    const auto f = fut::parse_form(form_request("multipart/form-data; boundary=x", body));
    REQUIRE(f.files.size() == 1);
    CHECK(f.files[0].filename == "\xE4\xBD\xA0.txt");   // 你.txt
}

TEST_CASE("UTF-8 validation rejects the invalid forms") {
    CHECK(fut::utf8_valid(""));
    CHECK(fut::utf8_valid("plain ascii"));
    CHECK(fut::utf8_valid("\xE4\xBD\xA0"));                  // U+4F60
    CHECK(fut::utf8_valid("\xF0\x9F\x99\x82"));             // emoji
    CHECK_FALSE(fut::utf8_valid("\xC0\xAF"));                 // overlong
    CHECK_FALSE(fut::utf8_valid("\xED\xA0\x80"));            // surrogate
    CHECK_FALSE(fut::utf8_valid("\xE4\xBD"));                 // truncated
    CHECK_FALSE(fut::utf8_valid("\xFF"));                      // never valid
}

TEST_CASE("a JSON body that is not UTF-8 is rejected") {
    struct name_dto { std::string name; };

    fut::request req{};
    req.body = "{\"name\":\"\xFF\xFE\"}";
    CHECK_FALSE(req.json<name_dto>().has_value());
    req.body = "{\"name\":\"yue\"}";
    const auto ok = req.json<name_dto>();
    REQUIRE(ok.has_value());
    CHECK(ok->name == "yue");
}
