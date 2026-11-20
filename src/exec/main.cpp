// fut_server entry point -- a thin shell over fut::exec::server_builder.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "builder.hpp"

int main(int argc, char** argv) {
    fut::exec::server_builder builder;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-c" && i + 1 < argc) builder.config_file(argv[++i]);
        else if (a == "--apps-dir" && i + 1 < argc) builder.apps_dir(argv[++i]);
        else if (a == "--port" && i + 1 < argc)
            builder.port(static_cast<unsigned short>(std::atoi(argv[++i])));
        else if (a == "--threads" && i + 1 < argc) builder.threads(std::atoi(argv[++i]));
        else if (a == "--log-level" && i + 1 < argc) builder.log_level(argv[++i]);
        else {
            std::fputs("usage: fut_server [-c config.json] [--apps-dir DIR]"
                       " [--port N] [--threads N] [--log-level LEVEL]\n", stderr);
            return 0;
        }
    }
    return builder.run();
}
