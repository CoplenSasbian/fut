# fut —— C++26 协程 Web 服务器（exec + 业务动态库）

主程序 \`\`fut_server.exe\`\` 负责监听、会话协程、加载业务动态库，**基本不用重编译**；
每个业务前缀（\`/api/admin\`、\`/api/web\`）是一个独立 app 动态库，**更新业务 = 编译对应 app → 复制 DLL**。

技术栈：**Asio（协程 awaitable）+ Boost.Beast（HTTP）+ C++26（std::meta 静态反射）+ glaze（反射式 JSON）+ doctest（测试）**。

## 目录结构

\`\`\`
fut/
├── CMakeLists.txt          根构建：C++26、-freflection、输出布局
├── CMakePresets.json       debug / release / sandbox 三套 preset
├── vcpkg.json              依赖清单：boost-beast、boost-asio、glaze、doctest
├── config.json             运行配置
├── include/fut/            ★ exec 与所有 app DLL 共享的接口（ABI 契约）
│   ├── app.hpp             request / response / fut::app 虚接口
│   ├── app_entry.hpp       FUT_IMPLEMENT_APP：导出 C ABI（create/destroy/abi_version）
│   ├── version.hpp         fut::abi_version —— 接口不兼容时 bump
│   ├── reflect.hpp         C++26 std::meta 工具（成员枚举/遍历）
│   ├── json.hpp            glaze 序列化帮助（fut::json / fut::parse / fut::error）
│   └── url.hpp             query 解析 + URL 解码
├── src/exec/               主程序（main / server / router / app_loader / config）
├── apps/admin/             /api/admin 业务库 → fut_app_admin.dll
├── apps/web/               /api/web   业务库 → fut_app_web.dll
└── tests/                  doctest：路由 / 反射 / DLL 全链路
\`\`\`

## 环境要求（本机已配好）

| 组件 | 位置 | 说明 |
| --- | --- | --- |
| GCC 16.2 | \`D:/mingw64\` | MinGW-Builds，支持 C++26 与 \`-freflection\` |
| vcpkg | \`D:/vcpkg/vcpkg\` | triplet 用 \`x64-mingw-dynamic\` |
| CMake 4.5.1 + Ninja | \`D:/fut/.tools\` | 便携版，随仓库走 |

\`.vcpkg/\` 是 vcpkg 的工作区克隆（约 200MB，已 gitignore），供 \`sandbox\` preset 使用。

## 构建

\`\`\`bash
# 常规构建（首次会下载安装 vcpkg 依赖，约 10 分钟；之后走二进制缓存秒装）
cmake --preset release
cmake --build --preset release
ctest --preset release

# 在 DSH 沙箱里构建用 sandbox preset（沙箱拦工作区外写入）：
#   它使用工作区内 .vcpkg 克隆、把 TMP/TEMP 重定向到 build/tmp、
#   并用 CMAKE_CXX_COMPILER_WORKS 跳过沙箱下会失败的编译器 try_compile 探测
cmake --preset sandbox
cmake --build --preset sandbox
ctest --preset sandbox
\`\`\`

产物（自包含，MinGW 运行时 DLL 已拷进 bin）：

\`\`\`
build/sandbox/bin/
├── fut_server.exe
├── libstdc++-6.dll  libgcc_s_seh-1.dll  libwinpthread-1.dll
├── test_*.exe
└── apps/
    ├── fut_app_admin.dll
    └── fut_app_web.dll
\`\`\`

## 运行

在仓库根目录（有 \`config.json\`）：

\`\`\`bash
./build/sandbox/bin/fut_server.exe
# 或自定义：fut_server -c other.json --apps-dir 某目录
\`\`\`

\`\`\`bash
curl http://127.0.0.1:8080/api/core/status
# {"service":"fut","version":"0.1.0","abi":1,"apps":[...三个 app...]}

curl http://127.0.0.1:8080/api/admin/meta        # C++26 std::meta 反射演示
# {"members":[{"name":"name","value_kind":"string"},...],"names":["name","rank","active","tags"]}

curl -X POST -d '{"message":"hi","count":4}' http://127.0.0.1:8080/api/admin/echo
curl "http://127.0.0.1:8080/api/web/hello?name=fut"
curl -X POST -d '{"a":6,"b":7,"op":"mul"}' http://127.0.0.1:8080/api/web/calc
\`\`\`

## 更新业务（核心工作流）

\`\`\`bash
cmake --build --preset sandbox --target fut_app_admin
# 把 build/sandbox/bin/apps/fut_app_admin.dll 复制到部署目录覆盖旧的，重启服务
\`\`\`

主程序与其它业务库完全不用重编译。

## 新增一个业务 app（例如 /api/pay）

1. 复制 \`apps/web\` 为 \`apps/pay\`，改 \`name()\`、\`prefix()\`（\`/api/pay\`）和 \`handle()\` 内的路由；
2. 根 \`CMakeLists.txt\` 加一行 \`add_subdirectory(apps/pay)\`；
3. 重新构建——exec 及其它 app 不受影响，服务器按前缀自动分发。

## ABI 约定（include/fut/）

* 所有模块必须用**同一编译器、同一运行时**构建（本仓库 preset 已保证）；
* \`request\` 中的 \`string_view\` 指向 exec 拥有的请求缓冲，仅在 \`handle()\` 执行期间有效；
* \`response\` 由 app 构造、所有权移交 exec；app 抛异常会被 exec 捕获转成 500；
* 接口不兼容修改时 **bump \`fut::abi_version\`**，\`app_loader\` 会拒绝加载 ABI 不符的旧 DLL；
* app 动态库只需链接 \`Boost::asio\`（awaitable）+ \`glaze\`，无需链接 Beast。

## 反射说明

* **std::meta（P2996，C++26 静态反射）**：GCC 15+ 需要 \`-freflection\`（根 CMakeLists 对 GNU 编译器自动添加）。工具见 \`include/fut/reflect.hpp\`：\`member_count_v\`、\`member_names_v\`、\`for_each_member\`；HTTP 演示见 \`GET /api/admin/meta\`。注意 std::meta 的查询结果（\`std::vector<meta::info>\`）只能活在常量求值内，故用 consteval + constexpr 变量模板固化。
* **glaze**：对聚合 DTO 做编译期反射自动 JSON 序列化（\`fut::json(dto)\` / \`fut::parse<T>(body)\`），无需手写字段映射。

## 配置 config.json

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| host | 127.0.0.1 | 监听地址 |
| port | 8080 | 监听端口 |
| threads | 2 | io_context 线程数（含主线程） |
| apps_dir | apps | 业务 DLL 目录；相对路径按 exe 所在目录解析 |
| request_timeout_seconds | 30 | 单请求读超时 |

## 测试

\`\`\`bash
ctest --preset sandbox   # 5 个用例：路由匹配×3、std::meta 反射、DLL 全链路
\`\`\`

DLL 全链路用例会真实加载两个业务 DLL，驱动协程调用 \`handle()\`，并用 glaze 校验返回 JSON。

> ctest 在 GBK 控制台显示中文用例名可能乱码，纯显示问题。
