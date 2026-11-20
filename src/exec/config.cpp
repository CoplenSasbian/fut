#include "config.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <glaze/glaze.hpp>

namespace fut::exec {

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

} // namespace fut::exec
