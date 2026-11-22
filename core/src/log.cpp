#include <fut/log.hpp>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <format>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fut/platform.hpp>

namespace fut::detail {

// glaze leaves control characters to its fast path, which emits NUL bytes and
// therefore invalid JSON, so escaping is requested explicitly.
struct json_opts : glz::opts {
    bool escape_control_characters = true;
};

}  // namespace fut::detail

namespace fut::log {

namespace {

// ---------------------------------------------------------------------------
// Records and formatters
// ---------------------------------------------------------------------------

/// One call, shared by every chain: the message is copied once, not per sink.
struct record {
    level severity{};
    std::string group;
    std::string message;
    std::string file;
    std::string function;
    std::uint32_t line{};
    std::uint32_t column{};
    std::int64_t timestamp_ns{};
    std::uint64_t thread_id{};
};

struct formatted {
    std::string text;
    level severity{level::trace};
};

class formatter_base {
public:
    virtual ~formatter_base() = default;
    [[nodiscard]] virtual formatted format(const record& entry) const = 0;
};

// One JSON object per line; the member order is the output order and the
// string_view fields are only valid while the record is written.
struct json_line {
    std::string_view level;
    std::string_view group;
    std::string_view message;
    std::string_view file;
    std::uint32_t line;
    std::string_view function;
    std::string_view time;
    std::uint64_t thread;
};

// Fields: {m} message, {t} time, {T} date and time, {l} level, {L} level in
// upper case, {g} group, {f} file relative to the project root, {F} full file
// path, {b} base file name, {n} line, {u} function, {U} full function
// signature, {h} thread id, {s} base file:line, {p} project file:line.
// Every field except the locations and {h} accepts a format spec after ':',
// and {t}/{T} accept a strftime pattern instead.
class pattern_formatter_impl final : public formatter_base {
public:
    explicit pattern_formatter_impl(std::string_view pattern) { parse(pattern); }

    [[nodiscard]] formatted format(const record& entry) const override;

    struct segment {
        enum class kind {
            literal,
            message,
            timestamp,
            date_time,
            level,
            level_upper,
            group,
            file,
            path_full,
            base_file,
            line,
            function,
            full_function,
            thread,
            location,
            short_location,
        };

        kind type{kind::literal};
        std::string text;  // literal text, or the format spec
    };

private:
    void parse(std::string_view pattern);

    std::vector<segment> m_segments;
};

class json_formatter_impl final : public formatter_base {
public:
    [[nodiscard]] formatted format(const record& entry) const override;
};

std::tm to_local_time(std::time_t stamp) noexcept {
    return fut::platform::local_time(stamp);
}

bool field_kind(std::string_view name, pattern_formatter_impl::segment::kind& result) noexcept {
    using field = pattern_formatter_impl::segment::kind;
    if (name == "m") { result = field::message; return true; }
    if (name == "t") { result = field::timestamp; return true; }
    if (name == "T") { result = field::date_time; return true; }
    if (name == "l") { result = field::level; return true; }
    if (name == "L") { result = field::level_upper; return true; }
    if (name == "g") { result = field::group; return true; }
    if (name == "f") { result = field::file; return true; }
    if (name == "F") { result = field::path_full; return true; }
    if (name == "b") { result = field::base_file; return true; }
    if (name == "n") { result = field::line; return true; }
    if (name == "u") { result = field::function; return true; }
    if (name == "U") { result = field::full_function; return true; }
    if (name == "h") { result = field::thread; return true; }
    if (name == "s") { result = field::short_location; return true; }
    if (name == "p") { result = field::location; return true; }
    return false;
}

bool takes_spec(pattern_formatter_impl::segment::kind type) noexcept {
    using field = pattern_formatter_impl::segment::kind;
    switch (type) {
    case field::timestamp:
    case field::date_time:
    case field::level:
    case field::level_upper:
    case field::group:
    case field::file:
    case field::path_full:
    case field::base_file:
    case field::function:
    case field::full_function: return true;
    default: return false;
    }
}

std::string upper_case(std::string_view text) {
    std::string result{text};
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

void pattern_formatter_impl::parse(std::string_view pattern) {
    using kind = segment::kind;

    std::string literal;
    auto flush_literal = [this, &literal] {
        if (literal.empty()) return;
        m_segments.push_back({kind::literal, std::move(literal)});
        literal.clear();
    };

    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char current = pattern[i];

        // "{{" and "}}" are the escapes for a literal brace.
        if (current == '{' && i + 1 < pattern.size() && pattern[i + 1] == '{') {
            literal += '{';
            ++i;
            continue;
        }
        if (current == '}' && i + 1 < pattern.size() && pattern[i + 1] == '}') {
            literal += '}';
            ++i;
            continue;
        }
        if (current == '}') throw std::invalid_argument("unmatched '}' in the log pattern");
        if (current != '{') {
            literal += current;
            continue;
        }

        const auto end = pattern.find('}', i + 1);
        if (end == std::string_view::npos) {
            throw std::invalid_argument("unterminated '{' in the log pattern");
        }

        const auto body = pattern.substr(i + 1, end - i - 1);
        const auto colon = body.find(':');
        const auto name = body.substr(0, colon);
        const auto spec = colon == std::string_view::npos ? std::string_view{} : body.substr(colon + 1);

        kind type{};
        if (!field_kind(name, type)) {
            throw std::invalid_argument("unknown field in the log pattern: {" + std::string(name)
                                        + "}");
        }
        if (!spec.empty() && !takes_spec(type)) {
            throw std::invalid_argument("this log field does not take a format spec");
        }

        flush_literal();
        m_segments.push_back({type, std::string(spec)});
        i = end;
    }
    flush_literal();
}

formatted pattern_formatter_impl::format(const record& entry) const {
    std::string out;
    out.reserve(128);

    for (const auto& piece : m_segments) {
        switch (piece.type) {
        case segment::kind::literal: out += piece.text; break;
        case segment::kind::message: out += entry.message; break;
        case segment::kind::level:
            out += fut::detail::format_text(to_string(entry.severity), piece.text);
            break;
        case segment::kind::level_upper:
            out += fut::detail::format_text(upper_case(to_string(entry.severity)),
                                               piece.text);
            break;
        case segment::kind::group:
            out += fut::detail::format_text(entry.group, piece.text);
            break;
        case segment::kind::file:
            out += fut::detail::format_text(fut::detail::relative_path(entry.file),
                                               piece.text);
            break;
        case segment::kind::path_full:
            out += fut::detail::format_text(entry.file, piece.text);
            break;
        case segment::kind::base_file:
            out += fut::detail::format_text(
                    fut::detail::base_file(fut::detail::relative_path(entry.file)),
                    piece.text);
            break;
        case segment::kind::line: out += std::to_string(entry.line); break;
        case segment::kind::function:
            out += fut::detail::format_text(
                    fut::detail::short_function_name(entry.function), piece.text);
            break;
        case segment::kind::full_function:
            out += fut::detail::format_text(entry.function, piece.text);
            break;
        case segment::kind::thread: out += std::to_string(entry.thread_id); break;
        case segment::kind::location:
            out += fut::detail::relative_path(entry.file);
            out += ':';
            out += std::to_string(entry.line);
            break;
        case segment::kind::short_location:
            out += fut::detail::base_file(entry.file);
            out += ':';
            out += std::to_string(entry.line);
            break;
        case segment::kind::date_time:
        case segment::kind::timestamp: {
            const auto clock = std::chrono::system_clock::time_point{
                    std::chrono::nanoseconds{entry.timestamp_ns}};
            const auto local = to_local_time(std::chrono::system_clock::to_time_t(clock));
            const bool date_and_time = piece.type == segment::kind::date_time;
            const char* fallback = date_and_time ? "%Y-%m-%d %H:%M:%S" : "%H:%M:%S";
            char buffer[128]{};
            std::strftime(buffer, sizeof(buffer),
                          piece.text.empty() ? fallback : piece.text.c_str(), &local);
            out += buffer;
            break;
        }
        }
    }

    return formatted{std::move(out), entry.severity};
}

formatted json_formatter_impl::format(const record& entry) const {
    char stamp[32]{};
    const auto clock = std::chrono::system_clock::time_point{
            std::chrono::nanoseconds{entry.timestamp_ns}};
    const auto local = to_local_time(std::chrono::system_clock::to_time_t(clock));
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &local);

    const json_line line{
            .level = to_string(entry.severity),
            .group = entry.group,
            .message = entry.message,
            .file = fut::detail::relative_path(entry.file),
            .line = entry.line,
            .function = fut::detail::short_function_name(entry.function),
            .time = stamp,
            .thread = entry.thread_id,
    };

    std::string out;
    out.reserve(256);
    // glaze leaves control characters to its fast path, which emits NUL bytes
    // and therefore invalid JSON, so escaping is requested explicitly.
    (void)glz::write<fut::detail::json_opts{}>(line, out);
    return formatted{std::move(out), entry.severity};
}

// ---------------------------------------------------------------------------
// Sinks
// ---------------------------------------------------------------------------

class sink_base {
public:
    virtual ~sink_base() = default;
    virtual void write(std::string_view text) noexcept = 0;
    virtual void write(const formatted& line) noexcept { write(line.text); }
    virtual void flush() noexcept = 0;
};

class console_sink_impl final : public sink_base {
public:
    explicit console_sink_impl(bool color) noexcept : m_color(color) {}

    void write(std::string_view text) noexcept override {
        // One fwrite per line keeps concurrent sinks from interleaving halves.
        char line[2048];
        if (text.size() < sizeof(line)) {
            if (!text.empty()) std::memcpy(line, text.data(), text.size());
            line[text.size()] = '\n';
            std::fwrite(line, 1, text.size() + 1, stdout);
        } else {
            if (!text.empty()) std::fwrite(text.data(), 1, text.size(), stdout);
            std::fputc('\n', stdout);
        }
    }

    void write(const formatted& line) noexcept override {
        if (!m_color) {          // plain: the base write adds the newline
            write(line.text);
            return;
        }
        // Colouring a console is the platform layer's business: console
        // attributes on Windows, ANSI escapes elsewhere, plain text when
        // the output is a pipe. One buffer, one write, newline included.
        std::string framed(line.text);
        framed.push_back('\n');
        fut::platform::console_write(framed, static_cast<int>(line.severity));
        std::fflush(stdout);
    }

    void flush() noexcept override { std::fflush(stdout); }

private:
    bool m_color{};
};

class file_sink_impl final : public sink_base {
public:
    file_sink_impl(std::filesystem::path path, std::uint64_t max_bytes, std::uint32_t max_files)
        : m_path(std::move(path)), m_max_bytes(max_bytes), m_max_files(max_files) {
        open();
    }

    ~file_sink_impl() override {
        std::lock_guard lock(m_mutex);
        close();
    }

    void write(std::string_view text) noexcept override {
        std::lock_guard lock(m_mutex);
        if (m_file == nullptr) return;

        const auto needed = static_cast<std::uint64_t>(text.size()) + 1;
        if (m_max_bytes != 0 && m_size != 0 && m_size + needed > m_max_bytes) rotate();
        if (m_file == nullptr) return;

        std::fwrite(text.data(), 1, text.size(), m_file);
        std::fputc('\n', m_file);
        m_size += needed;
    }

    void flush() noexcept override {
        std::lock_guard lock(m_mutex);
        if (m_file != nullptr) std::fflush(m_file);
    }

    [[nodiscard]] bool is_open() const noexcept { return m_file != nullptr; }

private:
    static std::filesystem::path backup_path(const std::filesystem::path& path,
                                             std::uint32_t index) {
        std::filesystem::path result{path};
        result += "." + std::to_string(index);
        return result;
    }

    void open() {
        std::error_code ignored;
        if (m_path.has_parent_path()) {
            std::filesystem::create_directories(m_path.parent_path(), ignored);
        }
        m_file = std::fopen(m_path.generic_string().c_str(), "ab");
        if (m_file == nullptr) {
            m_size = 0;
            return;
        }
        const auto length = std::filesystem::file_size(m_path, ignored);
        m_size = ignored ? 0 : static_cast<std::uint64_t>(length);
    }

    void close() noexcept {
        if (m_file == nullptr) return;
        std::fflush(m_file);
        std::fclose(m_file);
        m_file = nullptr;
    }

    void rotate() {
        close();

        const std::uint32_t keep = m_max_files == 0 ? 1 : m_max_files;
        std::error_code ignored;
        if (keep <= 1) {
            std::filesystem::remove(m_path, ignored);
        } else {
            std::filesystem::remove(backup_path(m_path, keep - 1), ignored);
            for (std::uint32_t index = keep - 1; index > 1; --index) {
                std::filesystem::rename(backup_path(m_path, index - 1),
                                        backup_path(m_path, index), ignored);
            }
            std::filesystem::rename(m_path, backup_path(m_path, 1), ignored);
        }

        m_size = 0;
        open();
    }

    std::filesystem::path m_path;
    std::uint64_t m_max_bytes{};
    std::uint32_t m_max_files{1};
    std::FILE* m_file{};
    std::uint64_t m_size{};
    std::mutex m_mutex;
};

/// Wraps a caller-provided sink; the chain does not own it.
class external_sink_impl final : public sink_base {
public:
    explicit external_sink_impl(std::shared_ptr<sink> target) noexcept
        : m_target(std::move(target)) {}

    void write(std::string_view text) noexcept override {
        if (m_target != nullptr) m_target->write(text);
    }

    void flush() noexcept override {
        if (m_target != nullptr) m_target->flush();
    }

private:
    std::shared_ptr<sink> m_target;
};

// ---------------------------------------------------------------------------
// Chains
// ---------------------------------------------------------------------------

using shared_record = std::shared_ptr<const record>;

struct queued {
    shared_record entry;
    std::shared_ptr<std::promise<void>> barrier;
};

class chain {
public:
    chain(chain_id id, level minimum, std::unique_ptr<formatter_base> formatter,
          std::unique_ptr<sink_base> destination)
        : m_id(id), m_minimum(minimum), m_formatter(std::move(formatter)),
          m_sink(std::move(destination)) {}

    chain(const chain&) = delete;
    chain& operator=(const chain&) = delete;

    ~chain() { stop(); }

    [[nodiscard]] level minimum_level() const noexcept { return m_minimum; }

    void start() {
        m_worker = std::jthread([this](std::stop_token token) { run(token); });
    }

    void push(shared_record entry) {
        {
            std::lock_guard lock(m_mutex);
            m_queue.push(queued{std::move(entry), {}});
        }
        m_cv.notify_one();
    }

    std::shared_ptr<std::promise<void>> barrier() {
        auto promise = std::make_shared<std::promise<void>>();
        {
            std::lock_guard lock(m_mutex);
            m_queue.push(queued{{}, promise});
        }
        m_cv.notify_one();
        return promise;
    }

    void stop() {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_cv.notify_all();
        if (m_worker.joinable()) m_worker.join();

        std::queue<queued> leftover;
        {
            std::lock_guard lock(m_mutex);
            leftover.swap(m_queue);
        }
        while (!leftover.empty()) {
            if (leftover.front().barrier) leftover.front().barrier->set_value();
            leftover.pop();
        }
    }

private:
    void run([[maybe_unused]] std::stop_token token) noexcept {
        for (;;) {
            queued item;
            bool drained = false;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this] { return !m_queue.empty() || m_stopping; });
                if (m_queue.empty() && m_stopping) break;

                item.entry = std::move(m_queue.front().entry);
                item.barrier = std::move(m_queue.front().barrier);
                m_queue.pop();

                // Nothing else is waiting: this is the end of a burst, which is
                // where one flush belongs. Flushing per record would cost an
                // fflush on every line; flushing only on a barrier leaves a
                // redirected stream empty for as long as the process runs.
                drained = m_queue.empty();
            }

            if (item.entry) {
                try {
                    m_sink->write(m_formatter->format(*item.entry));
                } catch (...) {
                }
            }
            if (item.barrier) {
                m_sink->flush();
                item.barrier->set_value();
            } else if (drained) {
                m_sink->flush();
            }
        }
        m_sink->flush();
    }

    chain_id m_id;
    level m_minimum;
    std::unique_ptr<formatter_base> m_formatter;
    std::unique_ptr<sink_base> m_sink;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<queued> m_queue;
    bool m_stopping{};
    std::jthread m_worker;
};

// ---------------------------------------------------------------------------
// The service
// ---------------------------------------------------------------------------

class service {
public:
    [[nodiscard]] chain_id add_chain(const chain_options& options) {
        level minimum = options.minimum_level;
        if (static_cast<std::int32_t>(minimum) < static_cast<std::int32_t>(level::trace)
            || static_cast<std::int32_t>(minimum) > static_cast<std::int32_t>(level::off)) {
            throw std::runtime_error(std::format("adding a log chain: unknown minimum level {}",
                                                 static_cast<std::int32_t>(minimum)));
        }

        try {
            auto formatter = make_formatter(options);
            auto destination = make_sink(options);
            if (formatter == nullptr) {
                throw std::runtime_error("adding a log chain: unknown formatter");
            }
            if (destination == nullptr) {
                throw std::runtime_error("adding a log chain: no usable sink was given");
            }

            const auto id = m_next_id.fetch_add(1);
            auto created = std::make_shared<chain>(id, minimum, std::move(formatter),
                                                   std::move(destination));
            created->start();

            std::lock_guard lock(m_mutex);
            m_chains.emplace(id, std::move(created));
            return id;
        } catch (const std::invalid_argument& failure) {
            throw std::runtime_error(std::format("adding a log chain: {}",
                                                 failure.what()));
        } catch (const std::exception& failure) {
            throw std::runtime_error(std::format("adding a log chain: {}",
                                                 failure.what()));
        } catch (...) {
            throw std::runtime_error("adding a log chain: the chain could not be created");
        }
    }

    void remove_chain(chain_id id) {
        std::shared_ptr<chain> removed;
        {
            std::lock_guard lock(m_mutex);
            const auto found = m_chains.find(id);
            if (found == m_chains.end()) return;
            removed = std::move(found->second);
            m_chains.erase(found);
        }
        removed->stop();
    }

    void clear_chains() {
        std::vector<std::shared_ptr<chain>> removed;
        {
            std::lock_guard lock(m_mutex);
            removed.reserve(m_chains.size());
            for (auto& item : m_chains) removed.push_back(std::move(item.second));
            m_chains.clear();
        }
        for (auto& item : removed) item->stop();
    }

    void submit(level severity, std::string_view group, std::string_view message,
                std::string_view file, std::string_view function, std::uint32_t line,
                std::uint32_t column, std::int64_t timestamp_ns, std::uint64_t thread_id) {
        if (static_cast<std::int32_t>(severity) < m_level.load(std::memory_order_relaxed)) return;

        try {
            // One copy for every chain, not one per chain.
            auto entry = std::make_shared<record>();
            entry->severity = severity;
            entry->group.assign(group);
            entry->message.assign(message);
            entry->file.assign(file);
            entry->function.assign(function);
            entry->line = line;
            entry->column = column;
            entry->timestamp_ns = timestamp_ns;
            entry->thread_id = thread_id;

            std::vector<std::shared_ptr<chain>> targets;
            {
                std::lock_guard lock(m_mutex);
                targets.reserve(m_chains.size());
                for (const auto& item : m_chains) {
                    if (static_cast<std::int32_t>(severity)
                        >= static_cast<std::int32_t>(item.second->minimum_level())) {
                        targets.push_back(item.second);
                    }
                }
            }

            if (targets.empty()) return;
            const shared_record shared = std::move(entry);
            for (const auto& target : targets) target->push(shared);
        } catch (...) {
            // A log call must never fail the caller.
        }
    }

    void set_level(level value) noexcept {
        if (static_cast<std::int32_t>(value) < static_cast<std::int32_t>(level::trace)
            || static_cast<std::int32_t>(value) > static_cast<std::int32_t>(level::off)) {
            return;
        }
        m_level.store(static_cast<std::int32_t>(value), std::memory_order_relaxed);
    }

    [[nodiscard]] level current_level() const noexcept {
        return static_cast<level>(m_level.load(std::memory_order_relaxed));
    }

    void flush() {
        std::vector<std::shared_ptr<chain>> chains;
        {
            std::lock_guard lock(m_mutex);
            chains.reserve(m_chains.size());
            for (const auto& item : m_chains) chains.push_back(item.second);
        }

        std::vector<std::future<void>> pending;
        pending.reserve(chains.size());
        for (const auto& item : chains) {
            try {
                pending.push_back(item->barrier()->get_future());
            } catch (...) {
            }
        }
        for (auto& item : pending) item.wait();
    }

private:
    static std::unique_ptr<formatter_base> make_formatter(const chain_options& options) {
        if (const auto* pattern = std::get_if<pattern_formatter>(&options.formatter)) {
            return std::make_unique<pattern_formatter_impl>(pattern->pattern);
        }
        if (std::holds_alternative<json_formatter>(options.formatter)) {
            return std::make_unique<json_formatter_impl>();
        }
        return nullptr;
    }

    static std::unique_ptr<sink_base> make_sink(const chain_options& options) {
        if (const auto* console = std::get_if<console_sink>(&options.sink)) {
            return std::make_unique<console_sink_impl>(console->color);
        }
        if (const auto* file = std::get_if<file_sink>(&options.sink)) {
            auto created = std::make_unique<file_sink_impl>(file->path, file->max_bytes,
                                                       file->max_files);
            if (!created->is_open()) {
                throw std::invalid_argument("the log file could not be opened: "
                                            + file->path.generic_string());
            }
            return created;
        }
        if (const auto* external = std::get_if<external_sink>(&options.sink)) {
            if (external->target == nullptr) {
                throw std::invalid_argument("the external sink is null");
            }
            return std::make_unique<external_sink_impl>(external->target);
        }
        return nullptr;
    }

    mutable std::mutex m_mutex;
    std::unordered_map<chain_id, std::shared_ptr<chain>> m_chains;
    std::atomic<chain_id> m_next_id{1};
    std::atomic<std::int32_t> m_level{static_cast<std::int32_t>(level::trace)};
};

service& instance() {
    // Deliberately leaked: a chain joins its worker thread in its destructor, and
    // a static destructor would run while the C runtime is already tearing stdio
    // down, which hangs the process on the final flush.
    static service* singleton = new service();
    return *singleton;
}

}  // namespace

sink::~sink() = default;

chain_id add_chain(const chain_options& options) {
    return instance().add_chain(options);
}

void remove_chain(chain_id id) {
    instance().remove_chain(id);
}

void clear_chains() {
    instance().clear_chains();
}

void set_level(level value) noexcept {
    instance().set_level(value);
}

level current_level() noexcept {
    return instance().current_level();
}

void flush() {
    instance().flush();
}

void shutdown() {
    instance().clear_chains();
}

void submit(level severity, std::string_view group, std::string_view message,
            std::string_view file, std::string_view function, std::uint32_t line,
            std::uint32_t column, std::int64_t timestamp_ns, std::uint64_t thread_id) {
    instance().submit(severity, group, message, file, function, line, column, timestamp_ns,
                      thread_id);
}

}  // namespace fut::log
