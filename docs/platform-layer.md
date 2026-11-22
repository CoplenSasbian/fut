# The platform layer

Everything operating-system specific in fut lives behind `fut/platform.hpp`.
The rest of the library -- server, router, parser, logging, apps -- is portable
C++26 and does not contain a single `#ifdef _WIN32`.

## The contract

| Declaration | What it must do |
| --- | --- |
| `library_suffixes()` | the file suffixes a loadable app library has on this platform |
| `is_library_name(name)` | suffix test, case-insensitive (**implemented once, in the header**) |
| `library::open(path, error)` | load, or leave the handle empty and put the platform's message in `error` |
| `library::symbol(name)` | a symbol, or `nullptr` when the library does not export it |
| `library::close()` | unload; safe on an empty handle and called by the destructor |
| `executable_path()` | absolute path of the running executable, or empty when the system cannot say |
| `utc_time(t)` / `local_time(t)` | thread-safe broken-down time (`gmtime_s` vs `gmtime_r`, ...) |
| `stdout_is_terminal()` | true when stdout is a console |
| `console_write(text, severity)` | write with the colour for a severity, restoring what was there |

Rules that keep this small and honest:

* **One implementation per platform.** `library` is a class in the header with
  `void* handle_`; only its methods are per-platform, so no layout knowledge
  leaks into the implementations.
* **No platform assumptions upward.** A missing feature returns empty/false --
  `executable_path()` on a system without `/proc` -- rather than throwing, and
  the caller decides what to do (the server falls back to the working directory).
* **Errors are data.** `open()` reports the system message; the loader logs it and
  skips the file. Nothing aborts.

## What exists today

| File | Platform | Covers |
| --- | --- | --- |
| `core/src/platform/windows.cpp` | Windows | `LoadLibraryW`, `GetProcAddress`, `FreeLibrary`, `GetModuleFileNameW`, `gmtime_s`/`localtime_s`, console attributes, `.dll` |
| `core/src/platform/posix.cpp` | Linux + macOS | `dlopen`, `dlsym`, `dlclose`, `gmtime_r`/`localtime_r`, ANSI escapes, `isatty` |
| `core/src/platform/linux.cpp` | Linux | `.so`, `/proc/self/exe` |
| `core/src/platform/macos.cpp` | macOS | `.dylib` (and `.so`), `_NSGetExecutablePath` |

`posix.cpp` holds what Linux and macOS share; the two thin files hold what they
do not. Windows has a single file because everything it needs is in one API
family.

## Adding a platform

1. **Implement the header.** Copy the closest implementation and satisfy every
   declaration in `fut/platform.hpp`. A platform that shares an API family with
   an existing one should reuse that file and add only its differences -- that is
   what `linux.cpp` and `macos.cpp` do on top of `posix.cpp`.
2. **Register the sources.** In `core/CMakeLists.txt`:

   ```cmake
   if(WIN32)
       target_sources(fut_core PRIVATE src/platform/windows.cpp)
   elseif(APPLE)
       target_sources(fut_core PRIVATE src/platform/posix.cpp src/platform/macos.cpp)
   elseif(FUT_PLATFORM_MYOS)
       target_sources(fut_core PRIVATE src/platform/myos.cpp)
   else()
       target_sources(fut_core PRIVATE src/platform/posix.cpp src/platform/linux.cpp)
   endif()
   ```

   The same list is needed by any target that compiles core sources directly
   (`tests/CMakeLists.txt` has one) -- keep them equal.
3. **Check the two deployment details.** A loaded app library must be able to
   find `libfut_core` at run time: on ELF/Mach-O that is the `$ORIGIN` rpath that
   `add_app()` sets; on a platform without rpaths it may be an environment
   variable or a per-directory lookup. The other detail is the suffix in
   `library_suffixes()`.
4. **Run the suite.** `tests/test_platform.cpp` is written against the interface,
   not against Win32 or POSIX: it checks that the suffixes name real libraries,
   that `is_library_name` agrees with them, that the executable can be found,
   that opening a bogus path fails with a message, and that the clock helpers
   agree with the C library. A new platform passes it by implementing the header.

Nothing else in the tree needs to change: `fut::exec::app_loader` asks the
platform layer for suffixes and handles, `fut::exec::server` asks it for the
executable's directory, the logger asks it for colours and local time, and the
HTTP Date header asks it for UTC.

## Why not a library like libuv or Boost.DLL

Boost.DLL would cover the loading part, but not the console, the clock or the
executable path, and at that point the layer is four functions and one class --
less code than the glue to integrate a dependency, with no extra DLL to ship and
no build-configuration surface to support.

## The audit

Every file outside `core/src/platform/` was checked for the things that
usually break a port, and the guard below keeps the result from drifting.

| Found | Where | Fixed how |
| --- | --- | --- |
| `strftime` for the HTTP `Date` header | `http_message.cpp` | the header is built from fixed English tables: `strftime` honours `LC_TIME`, and a German or French locale would have produced an invalid `Date` |
| `SO_REUSEADDR` set unconditionally | `listener.cpp` | `platform::reuse_address_is_safe()`: true on POSIX (reuse a port in `TIME_WAIT`), false on Windows (the same option lets another process take the port) |
| no `-pthread` on POSIX | `core/CMakeLists.txt` | `find_package(Threads)` + `Threads::Threads` linked **PUBLIC** from `fut_core`, so every executable and app library inherits it |
| `LoadLibraryA` / `dlopen` split in the loader | `app_loader.cpp` | the platform layer (this document) |
| `/proc/self/exe` only | `server.cpp` | `platform::executable_path()` with a macOS branch |
| MinGW-only configure presets | build presets | `CMakePresets.example.json` carries a `linux` and a `linux-debug` family next to the MinGW ones, and the presets file itself is machine-local (ignored, copied from the template), so no one's toolchain path is committed |

Checked and already portable:

* **File IO** is binary everywhere (`std::ifstream(..., std::ios::binary)` in the
  config reader, `fopen(..., "ab")` for log files), so no CRLF translation.
* **Log rotation** closes the file before rename/remove -- on Windows an open
  file cannot be renamed, on POSIX it can, and the code does the safe thing on
  both.
* **Paths** are `std::filesystem` values; no hard-coded separators, no drive
  letters. The only literal path is the default `logs/fut.log`, which is
  resolved by the same code on every platform.
* **Sockets, timers, signals** go through Asio, which owns the per-platform
  details; `ws2_32`/`mswsock` are linked inside `if(WIN32)` only.
* **Time** is `std::time_t`/`std::tm` (no `long`, no `timeval`) through the
  platform layer.
* **Tests** take their directories from `std::filesystem::temp_directory_path()`
  and from `FUT_APPS_DIR`, never from a literal.

## The guard

`cmake/check_portability.cmake` is registered as the `platform-audit` test: it
scans every source outside `core/src/platform/` (and `fut/export.hpp`, which
speaks to compilers rather than systems) for `_WIN32`, `__APPLE__`,
`__linux__`, `_MSC_VER`, `unistd.h` and `windows.h`, and **fails the suite** if
one appears. A Windows-only shortcut therefore cannot be committed without the
build telling you where the platform layer expects it.
