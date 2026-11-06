# WebAssembly compilation

The Emscripten backend builds the complete C++ API and generated C ABI into a
WebAssembly static archive. Shared logic (geometry, layout, handle management,
events, positioning and validation) remains available. This backend provides
compilation compatibility, not browser implementations of desktop services.

## Build and test

Install and activate [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(verified with 4.0.23), then run from the core repository, or replace `-S .` with
`-S core` from the workspace root:

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
ctest --test-dir build-wasm --output-on-failure
```

The library is `build-wasm/src/libnativeapi.a`; it contains WebAssembly objects,
not host machine code. Tests produce `.js` launchers and `.wasm` modules and run
under the SDK's Node.js through CMake's cross-compiling emulator. The smoke test
links every library object, including the entire generated C ABI, to detect missing
platform symbols. Five additional tests cover shared geometry and layout math.

Desktop examples default to disabled on Emscripten (`NATIVEAPI_BUILD_EXAMPLES=OFF`)
because some require host libraries or native event loops. To build only the
archive, configure with `-DBUILD_TESTING=OFF`.

## Embed in a CMake application

```cmake
set(NATIVEAPI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(nativeapi)
add_executable(app main.cpp) # main.c can use the C ABI through nativeapi.h
target_link_libraries(app PRIVATE nativeapi)
```

Configure the application with `emcmake cmake`. The target propagates C++17 and
`-fexceptions` for compilation and linking: shared code and C ABI wrappers catch
exceptions, so Emscripten's default disabled catching is unsuitable. Do not mix
this archive with a different exception mode.

Emscripten's [build documentation](https://emscripten.org/docs/compiling/Building-Projects.html)
describes linking an archive into JavaScript/WebAssembly outputs. A standalone
JavaScript package, exported JavaScript functions and browser UI integration are
not provided; an embedder chooses which C ABI functions to export and how to
marshal strings, handles and callbacks.

## Runtime behavior

- Window/display enumeration returns empty; window wrappers expose no native
  handle and an invalid ID, even when passed a pointer. Native window operations
  do nothing or return `false`.
- Native views, trays, global shortcuts, keyboard monitoring, drag and drop,
  file/message dialogs, notifications, launch-at-login, URL opening and
  accessibility integration are unavailable. Support queries return `false`.
- Preferences and secure storage do not persist data. Mutations return `false`,
  reads return the caller's default, and secure storage reports unavailable.
- Clipboard reads complete with failure and an empty payload. Writes and
  monitoring return `false`. No browser permission requests are made.
- Image decoding and native image handles are unavailable: factories return
  `nullptr`. App and host device metadata is empty, except the process
  architecture (`wasm32`, or `wasm64` when built for memory64).
- JavaScript owns the event loop. `Application::Run()` returns immediately,
  `IsRunning()` is false, and `Quit()` never exits the JavaScript host.
  No native main-thread scheduler or nested event loop is advertised. An embedder
  needing asynchronous event delivery must install `SetMainThreadDispatcher()`.

The default build needs no pthread workers or cross-origin isolation. WASI,
Flutter/Dart Web and the Node-API addon are separate targets and are not enabled
by this backend.
