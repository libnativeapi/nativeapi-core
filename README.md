# nativeapi

A cross-platform C++ library providing unified access to native system APIs — windows, tray icons, menus, displays, keyboard, dialogs, storage and more. It also exposes a C ABI that the language bindings are generated from.

| Linux | macOS | Windows |
|:-----:|:-----:|:-------:|
| ✅ | ✅ | ✅ |

🚧 **Work in Progress**: this library is under active development.

Language bindings: [Flutter](https://github.com/libnativeapi/nativeapi-flutter) · [Rust](https://github.com/libnativeapi/nativeapi-rust) · [C#](https://github.com/libnativeapi/nativeapi-csharp)

## Installation

Requires CMake 3.10+ and a C++17 compiler. On Linux, install the GTK 3 headers (`sudo apt install libgtk-3-dev`).

Add the repository to your project and link the `nativeapi` target:

```cmake
add_subdirectory(nativeapi)
target_link_libraries(your_app PRIVATE nativeapi)
```

On Windows, configure with `-DNATIVEAPI_ENABLE_WINUI3=ON` to use the WinUI 3 backends for menus, dialogs, title bars and notifications — see [docs/winui3.md](docs/winui3.md).

On Linux, window control policies depend on the window manager and decoration type — see [docs/linux-window-controls.md](docs/linux-window-controls.md) for supported behavior and compositor limitations.

## Quick Start

```cpp
#include <iostream>
#include "nativeapi.h"

using namespace nativeapi;

int main() {
  for (const auto& display : DisplayManager::GetInstance().GetAll()) {
    auto size = display->GetSize();
    std::cout << display->GetName() << ": " << size.width << "x" << size.height << "\n";
  }
}
```

## Examples

See [`examples/`](examples). Each directory is a standalone program for one module (C++ and C variants), built together with the library:

```bash
cmake -S . -B build
cmake --build build
./build/examples/display_example/display_example
```

## Contributing

Development happens in [nativeapi](https://github.com/libnativeapi/nativeapi), which holds every binding and the code generator and checks out the core library as a submodule:

```bash
git clone --recursive https://github.com/libnativeapi/nativeapi.git
```

Files marked `AUTO-GENERATED. DO NOT EDIT.` are generated from the C++ headers in [nativeapi](https://github.com/libnativeapi/nativeapi-core). To change the API, send a pull request there; maintainers regenerate the bindings.

- API requests and native behavior bugs → [nativeapi-core issues](https://github.com/libnativeapi/nativeapi-core/issues)
- Bugs specific to one binding → [nativeapi issues](https://github.com/libnativeapi/nativeapi/issues)
- Not sure → [nativeapi-core issues](https://github.com/libnativeapi/nativeapi-core/issues)

## License

[MIT](./LICENSE)
