#pragma once

// fut/log.hpp -- asynchronous, sink-based logging.
//
// A call formats its message on the calling thread and hands it to the
// service, which copies it once into a shared record and queues that record
// to every chain. A chain owns a worker thread that formats and writes, so a
// slow sink never blocks the caller.
//
// Level names come straight from the declaration through std::meta reflection
// (enumerators_of + identifier_of), so no name table has to be kept in sync.
//
// Call fut::log::shutdown() (or flush()) before main returns: the module
// deliberately never joins its worker threads during static destruction,
// because the C runtime has already started tearing stdio down by then.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#include <meta>

// Reflection is a hard requirement: no reflection-free fallback.
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ < 15
#  error "fut/log.hpp requires C++26 std::meta reflection (GCC 15+ with -freflection)"
#endif

#include <fut/detail/format.hpp>
#include <fut/detail/path.hpp>

#ifndef FUT_LOG_API
#  define FUT_LOG_API
#endif

namespace fut::log {

enum class level : std::int32_t {
    trace = 0,
    debug = 1,
    info = 2,
    warning = 3,
    error = 4,
    critical = 5,
    off = 6,
};

/// The name of an enumerator, read from its declaration through reflection;
/// "unknown" when the value is not an enumerator (a bitwise combination, say).
template <class Enum>
    requires std::is_enum_v<Enum>
constexpr std::string_view to_string(Enum value) {
    template for (constexpr auto e :
                  std::define_static_array(std::meta::enumerators_of(^^Enum))) {
        if (std::meta::extract<Enum>(e) == value) return std::meta::identifier_of(e);
    }
    return "unknown";
}

/// The reverse of to_string(); std::nullopt for a name no enumerator holds.
template <class Enum>
    requires std::is_enum_v<Enum>
constexpr std::optional<Enum> from_string(std::string_view name) {
    template for (constexpr auto e :
                  std::define_static_array(std::meta::enumerators_of(^^Enum))) {
        if (std::meta::identifier_of(e) == name) return std::meta::extract<Enum>(e);
    }
    return std::nullopt;
}

/// A colourised stdout sink, built inside the module.
struct console_sink {
    bool color = false;
};

/// A rotating file sink, built inside the module. The parent directory is created.
struct file_sink {
    std::filesystem::path path{"fut.log"};

    /// Rotate once the file would exceed this size; 0 disables rotation.
    std::uint64_t max_bytes{};

    /// Backups kept as "<path>.1" ... "<path>.<max_files-1>"; 1 means no backup.
    std::uint32_t max_files{1};
};

/// A destination implemented by the caller. It must outlive the chain.
class FUT_LOG_API sink {
public:
    sink() = default;
    sink(const sink&) = delete;
    sink& operator=(const sink&) = delete;

    /// Called on the chain's worker thread; must not throw.
    virtual void write(std::string_view text) noexcept = 0;
    virtual void flush() noexcept = 0;

    virtual ~sink();
};

/// A sink the caller implements. The chain keeps it alive while it exists.
struct external_sink {
    std::shared_ptr<sink> target;
};

struct pattern_formatter {
    std::string pattern;
};

struct json_formatter {};

/// One chain: a formatter plus a destination, plus its own level filter.
struct chain_options {
    level minimum_level{level::trace};
    std::variant<pattern_formatter, json_formatter> formatter;
    std::variant<console_sink, file_sink, external_sink> sink;
};

using chain_id = std::uint64_t;

/// Registers a chain and starts its worker thread; throws std::runtime_error
/// when the pattern or the sink is unusable. The returned id is only needed
/// when the chain is removed later; process-lifetime chains may discard it.
FUT_LOG_API chain_id add_chain(const chain_options& options);

/// Removes a chain, flushing it and joining its thread before returning.
FUT_LOG_API void remove_chain(chain_id id);

FUT_LOG_API void clear_chains();

/// Global level: calls below it are dropped before formatting.
FUT_LOG_API void set_level(level value) noexcept;
[[nodiscard]] FUT_LOG_API level current_level() noexcept;

/// Blocks until every chain has written what was submitted before the call.
FUT_LOG_API void flush();

/// Flushes and drops all chains. Call this before main returns.
FUT_LOG_API void shutdown();

/// The write path every helper below funnels into. The text is only borrowed
/// for the duration of the call; the service copies what it keeps.
FUT_LOG_API void submit(level severity,
                        std::string_view group,
                        std::string_view message,
                        std::string_view file,
                        std::string_view function,
                        std::uint32_t line,
                        std::uint32_t column,
                        std::int64_t timestamp_ns,
                        std::uint64_t thread_id);

template <class... Args>
void write(level severity,
           std::string_view group,
           fut::detail::location_fmt_string<Args...> pattern,
           Args&&... args) {
    if (static_cast<std::int32_t>(severity) < static_cast<std::int32_t>(current_level())) return;

    submit(severity, group, pattern.format(std::forward<Args>(args)...),
           fut::detail::relative_path(pattern.location.file_name()),
           fut::detail::short_function_name(pattern.location.function_name()),
           pattern.location.line(), pattern.location.column(),
           std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count(),
           static_cast<std::uint64_t>(
                   std::hash<std::thread::id>{}(std::this_thread::get_id())));
}

template <class... Args>
void trace(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) {
    write(level::trace, {}, pattern, std::forward<Args>(args)...);
}

template <class... Args>
void debug(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) {
    write(level::debug, {}, pattern, std::forward<Args>(args)...);
}

template <class... Args>
void info(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) {
    write(level::info, {}, pattern, std::forward<Args>(args)...);
}

template <class... Args>
void warning(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) {
    write(level::warning, {}, pattern, std::forward<Args>(args)...);
}

template <class... Args>
void error(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) {
    write(level::error, {}, pattern, std::forward<Args>(args)...);
}

template <class... Args>
void critical(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) {
    write(level::critical, {}, pattern, std::forward<Args>(args)...);
}

/// A named category that prefixes every call without touching the global
/// functions:  constexpr auto net = fut::log::grp("net");  net.info("...");
struct group_ref {
    std::string_view name;

    template <class... Args>
    void trace(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) const {
        write(level::trace, name, pattern, std::forward<Args>(args)...);
    }

    template <class... Args>
    void debug(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) const {
        write(level::debug, name, pattern, std::forward<Args>(args)...);
    }

    template <class... Args>
    void info(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) const {
        write(level::info, name, pattern, std::forward<Args>(args)...);
    }

    template <class... Args>
    void warning(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) const {
        write(level::warning, name, pattern, std::forward<Args>(args)...);
    }

    template <class... Args>
    void error(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) const {
        write(level::error, name, pattern, std::forward<Args>(args)...);
    }

    template <class... Args>
    void critical(fut::detail::location_fmt_string<Args...> pattern, Args&&... args) const {
        write(level::critical, name, pattern, std::forward<Args>(args)...);
    }
};

inline constexpr group_ref grp(std::string_view name) noexcept { return group_ref{name}; }

inline constexpr group_ref core_log = grp("core");

}  // namespace fut::log
