#include "config.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <glaze/glaze.hpp>

namespace fut::exec {
namespace {

std::string where(std::size_t index) { return "log.chains[" + std::to_string(index) + "]"; }

fut::log::level parse_level(std::string_view text, std::size_t index) {
    if (const auto level = fut::log::from_string<fut::log::level>(text)) return *level;
    throw std::runtime_error(where(index) + ": unknown level \"" + std::string(text) +
                             "\" (expected trace, debug, info, warning, error, critical "
                             "or off)");
}

fut::log::chain_options build_chain(const log_chain_config& chain, std::size_t index) {
    fut::log::chain_options options{};
    options.minimum_level = parse_level(chain.level, index);

    if (chain.formatter == "pattern") {
        options.formatter = fut::log::pattern_formatter{chain.pattern};
    } else if (chain.formatter == "json") {
        options.formatter = fut::log::json_formatter{};
    } else {
        throw std::runtime_error(where(index) + ": unknown formatter \"" + chain.formatter +
                                 "\" (expected \"pattern\" or \"json\")");
    }

    if (chain.sink == "console") {
        options.sink = fut::log::console_sink{.color = chain.color};
    } else if (chain.sink == "file") {
        if (chain.path.empty())
            throw std::runtime_error(where(index) + ": sink \"file\" needs a non-empty path");
        if (chain.max_files == 0)
            throw std::runtime_error(where(index) + ": max_files must be at least 1");
        options.sink = fut::log::file_sink{.path = chain.path,
                                           .max_bytes = chain.max_bytes,
                                           .max_files = chain.max_files};
    } else {
        throw std::runtime_error(where(index) + ": unknown sink \"" + chain.sink +
                                 "\" (expected \"console\" or \"file\")");
    }
    return options;
}

} // namespace

config load_config(const std::filesystem::path& path) {
    config cfg{};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return cfg;

    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open config file: " + path.string());
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

    if (bool(glz::read_json(cfg, text)))
        throw std::runtime_error("invalid JSON in config file: " + path.string());
    return cfg;
}

std::vector<fut::log::chain_options> build_log_chains(const log_config& cfg) {
    std::vector<fut::log::chain_options> out;
    out.reserve(cfg.chains.size());
    for (std::size_t i = 0; i < cfg.chains.size(); ++i)
        out.push_back(build_chain(cfg.chains[i], i));
    return out;
}

} // namespace fut::exec
