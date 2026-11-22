#pragma once

// fut/http/form.hpp -- form bodies: application/x-www-form-urlencoded and
// multipart/form-data.
//
// A form is parsed once per request, and only when an action asks for one of
// its fields (the binding annotations from_form / from_file). Everything is
// header-only and **borrowed**: every field and file is a view into the
// request body, which the server keeps alive for the whole dispatch. Nothing is
// copied, so a 30 MB upload stays one 30 MB buffer. Carry the values you keep
// (std::string(field)) into your own structures if they must outlive the
// request.
//
//   struct [[=fut::controller{}]] uploads {
//       [[=fut::http_post("/upload")]]
//       awaitable<out> upload([[=fut::from_form{}]] std::string title,
//                             [[=fut::from_file{}]] fut::uploaded_file file);
//   };

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fut/http/bind.hpp>
#include <fut/http/route.hpp>
#include <fut/url.hpp>

namespace fut {

// One uploaded file: a multipart part that carries a filename. All four views
// point into the request body (and, for the two header-derived ones, into the
// request's header strings), so they live exactly as long as the request does.
struct uploaded_file {
    std::string_view name{};          // form field name
    std::string_view filename{};      // client-supplied file name
    std::string_view content_type{};  // part's Content-Type (empty when absent)
    std::string_view data{};          // the bytes

    [[nodiscard]] bool empty() const { return data.empty(); }
    [[nodiscard]] std::size_t size() const { return data.size(); }
};

// A parsed form: text fields plus uploaded files, in body order.
struct form_data {
    // Decoded text for urlencoded bodies. A deque because its elements keep
    // their addresses when the form itself is moved into the caller.
    std::deque<std::string> owned{};
    std::vector<std::pair<std::string_view, std::string_view>> fields{};
    std::vector<uploaded_file> files{};

    [[nodiscard]] const std::string_view* field(std::string_view name) const {
        for (const auto& [key, value] : fields)
            if (key == name) return &value;
        return nullptr;
    }

    [[nodiscard]] const uploaded_file* file(std::string_view name) const {
        for (const auto& f : files)
            if (f.name == name) return &f;
        return nullptr;
    }
};

namespace detail {

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

// A Content-Type parameter: boundary=..., possibly quoted, possibly followed
// by other parameters.
inline std::string_view content_type_param(std::string_view content_type,
                                           std::string_view name) {
    std::size_t pos = 0;
    while ((pos = content_type.find(name, pos)) != std::string_view::npos) {
        const bool at_start = pos == 0 || content_type[pos - 1] == ';' ||
                              content_type[pos - 1] == ' ';
        pos += name.size();
        if (!at_start) continue;
        while (pos < content_type.size() && content_type[pos] == ' ') ++pos;
        if (pos >= content_type.size() || content_type[pos] != '=') continue;
        ++pos;
        while (pos < content_type.size() && content_type[pos] == ' ') ++pos;

        if (pos < content_type.size() && content_type[pos] == '"') {
            const std::size_t end = content_type.find('"', pos + 1);
            if (end == std::string_view::npos) return {};
            return content_type.substr(pos + 1, end - pos - 1);
        }
        std::size_t end = pos;
        while (end < content_type.size() && content_type[end] != ';' &&
               content_type[end] != ' ')
            ++end;
        return content_type.substr(pos, end - pos);
    }
    return {};
}

// name="a"; filename="b" from a Content-Disposition value.
inline void parse_disposition(std::string_view value, uploaded_file& part) {
    std::size_t pos = 0;
    while ((pos = value.find('=', pos)) != std::string_view::npos) {
        std::size_t begin = pos;
        while (begin > 0 && value[begin - 1] != ';' && value[begin - 1] != ' ') --begin;
        const std::string_view token = trim(value.substr(begin, pos - begin));

        std::size_t vp = pos + 1;
        while (vp < value.size() && value[vp] == ' ') ++vp;

        std::string_view parsed;
        if (vp < value.size() && value[vp] == '"') {
            const std::size_t end = value.find('"', vp + 1);
            if (end == std::string_view::npos) return;
            parsed = value.substr(vp + 1, end - vp - 1);
            pos = end + 1;
        } else {
            std::size_t end = vp;
            while (end < value.size() && value[end] != ';') ++end;
            parsed = trim(value.substr(vp, end - vp));
            pos = end;
        }

        // Views into the part's header block, which lives in the body.
        if (token == "name") part.name = parsed;
        else if (token == "filename") part.filename = parsed;
    }
}

} // namespace detail

// "a=1&b=hello+world" -- %XX and '+' decoded; the first occurrence wins.
//
// urlencoded is the one form that cannot borrow: decoding produces new bytes.
// They are parked in form.owned, and the field views point there.
inline void parse_urlencoded(std::string_view body, form_data& out) {
    for (auto& [key, value] : ::fut::parse_query(body)) {
        if (out.field(key) != nullptr) continue;
        out.owned.push_back(std::move(key));
        std::string& stored_key = out.owned.back();
        std::string& stored_value = out.owned.emplace_back(std::move(value));
        out.fields.emplace_back(stored_key, stored_value);
    }
}

// multipart/form-data. Returns false and sets why when the body is malformed.
inline bool parse_multipart(std::string_view body, std::string_view content_type,
                            form_data& out, std::string& why) {
    const std::string_view boundary = detail::content_type_param(content_type, "boundary");
    if (boundary.empty()) {
        why = "missing boundary parameter";
        return false;
    }

    const std::string delimiter = "--" + std::string(boundary);
    std::size_t pos = body.find(delimiter);
    if (pos == std::string_view::npos) {
        why = "boundary not found in body";
        return false;
    }

    for (;;) {
        pos += delimiter.size();
        if (body.substr(pos, 2) == "--") return true;          // closing delimiter
        if (body.substr(pos, 2) == "\r\n") pos += 2;
        else if (pos < body.size() && body[pos] == '\n') pos += 1;

        const std::size_t header_end = body.find("\r\n\r\n", pos);
        if (header_end == std::string_view::npos) {
            why = "unterminated part headers";
            return false;
        }
        const std::string_view headers = body.substr(pos, header_end - pos);
        const std::size_t data_begin = header_end + 4;

        const std::size_t next = body.find(delimiter, data_begin);
        if (next == std::string_view::npos) {
            why = "unterminated part data";
            return false;
        }
        std::size_t data_end = next;
        if (data_end >= data_begin + 2 && body.substr(data_end - 2, 2) == "\r\n")
            data_end -= 2;
        else if (data_end > data_begin && body[data_end - 1] == '\n')
            data_end -= 1;

        uploaded_file part{};
        std::string_view head = headers;
        while (!head.empty()) {
            const std::size_t eol = head.find('\n');
            std::string_view line = (eol == std::string_view::npos) ? head
                                                                    : head.substr(0, eol);
            head = (eol == std::string_view::npos) ? std::string_view{}
                                                   : head.substr(eol + 1);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos) continue;
            const std::string_view name = detail::trim(line.substr(0, colon));
            const std::string_view value = detail::trim(line.substr(colon + 1));

            if (detail::iequals(name, "content-disposition")) {
                detail::parse_disposition(value, part);
            } else if (detail::iequals(name, "content-type")) {
                part.content_type = value;
            }
        }

        const std::string_view data = body.substr(data_begin, data_end - data_begin);
        if (part.filename.empty()) {
            out.fields.emplace_back(part.name, data);
        } else {
            part.data = data;
            out.files.push_back(part);
        }
        pos = next;
    }
}

// Parses the request's form body according to its Content-Type. Throws
// fut::bind_error when the body is not a usable form.
inline form_data parse_form(const request& req) {
    const std::string_view content_type = req.header("content-type");
    form_data out;

    if (content_type.rfind("multipart/form-data", 0) == 0) {
        std::string why;
        if (!parse_multipart(req.body, content_type, out, why))
            throw ::fut::bind_error("invalid multipart body: " + why);
        return out;
    }
    if (content_type.rfind("application/x-www-form-urlencoded", 0) == 0) {
        parse_urlencoded(req.body, out);
        return out;
    }
    throw ::fut::bind_error(
        "form binding needs 'application/x-www-form-urlencoded' or "
        "'multipart/form-data' (got '" + std::string(content_type) + "')");
}

} // namespace fut
