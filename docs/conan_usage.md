# Conan Usage

## Quick Start

Add to your `conanfile.txt`:
```ini
[requires]
jsonfusion/1.0.1

[generators]
CMakeDeps
CMakeToolchain

[layout]
cmake_layout
```

In your `CMakeLists.txt`:
```cmake
find_package(JsonFusion REQUIRED CONFIG)
target_link_libraries(your_target JsonFusion::JsonFusion)
target_compile_features(your_target PUBLIC cxx_std_23)
```

Install and build:
```bash
conan install . --output-folder=build --build=missing
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Creating the Package Locally

From the JsonFusion root:
```bash
conan create . --build=missing --test-folder=packaging/conan/test_package
```

The version is automatically extracted from `include/JsonFusion/version.hpp`.

## Version Management

Update only `include/JsonFusion/version.hpp`:
```cpp
#define JSONFUSION_VERSION "1.1.0"
```

Then `conan create .` will automatically use the new version.

## Publishing

### To Conan Center (Public)
1. Create a GitHub release: `git tag v1.0.1 && git push origin v1.0.1`
2. Fork [conan-center-index](https://github.com/conan-io/conan-center-index)
3. Follow their [contribution guide](https://github.com/conan-io/conan-center-index/blob/master/docs/how_to_add_packages.md)
4. Submit a pull request

### To Private Remote
```bash
conan remote add myremote <url>
conan remote login myremote
conan upload jsonfusion/1.0.1 -r=myremote --all
```

## Requirements

- Conan 2.x: `pip install conan`
- C++23 compatible compiler (GCC 14+)

For a complete working example, see `packaging/conan/example/` directory.
