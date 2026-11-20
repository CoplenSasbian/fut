#pragma once

#include <filesystem>
#include <vector>

#include <fut/app.hpp>

namespace fut::exec {

// 从目录加载 app 动态库（Windows: LoadLibrary / POSIX: dlopen）。
// 扫描目录下所有动态库，只接受导出 fut_abi_version / fut_app_create /
// fut_app_destroy 且 ABI 版本相符的库；其余（非 app 库、旧 ABI）跳过并告警。
class app_loader {
public:
    struct entry {
        std::filesystem::path dll{};
        fut::app* instance = nullptr;
        void* handle = nullptr;
        void (*destroy)(fut::app*) = nullptr;
    };

    std::vector<entry> load_dir(const std::filesystem::path& dir);

    // 销毁所有实例并卸载库（进程退出时调用）
    void unload_all();

    ~app_loader();
    app_loader() = default;
    app_loader(const app_loader&) = delete;
    app_loader& operator=(const app_loader&) = delete;

private:
    std::vector<entry> loaded_;
};

} // namespace fut::exec
