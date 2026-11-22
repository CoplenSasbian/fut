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
