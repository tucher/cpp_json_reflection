# Getting Started

This tutorial takes you from zero to a working JSON parse in a few steps.
Every code block below is interactive — click **Compile** to compile it right in your browser. Because JsonFusion is `constexpr`, you can use `static_assert` for tests instead of `assert`: testing/playing without running any binaries. If the assertion fails, you'll see a compilation error.

## Setup

JsonFusion is header-only. Clone the repository and point your compiler to the `include/` directory:

```bash
git clone https://github.com/tucher/JsonFusion.git
cd JsonFusion
```

Save any example below as `example.cpp`, then compile and run:

```bash
g++ -std=c++23 -I include example.cpp -o example && ./example
```

**Requirements:** GCC 14+ or Clang 20+ with C++23 support. (For the optional C++26 reflection mode, use GCC 16+ with `-std=c++26 -freflection`.)

## Your First Parse

Define a C++ struct and parse JSON into it. No macros, no registration — just a struct:

```cpp live check-only
#include <JsonFusion/parser.hpp>

struct Config {
    int port;
    std::string host;
};

constexpr bool test() {
    Config conf;
    if(!JsonFusion::Parse(conf, R"({"port": 8080, "host": "localhost"})")) {
        return false;
    }
    return conf.port == 8080 && conf.host == "localhost";
}

static_assert(test());

int main() { return test() ? 0: 1; }
```

The `Parse(object, data)` function returns a bool-convertible result. 

> Try changing `8080` to another value in the assertion and recompile — the `static_assert` will catch the mismatch:

```cpp live check-only
#include <JsonFusion/parser.hpp>

struct Config {
    int port;
    std::string host;
};

constexpr bool test() {
    Config conf;
    if(!JsonFusion::Parse(conf, R"({"port": 8081, "host": "localhost"})")) {
        return false;
    }
    return conf.port == 8080; // mismatch — will fail!
}

static_assert(test());

int main() { return test() ? 0: 1; }
```

## Serialization

Converting back to JSON is just as simple:

```cpp live check-only
#include <JsonFusion/serializer.hpp>
#include <vector>

struct Point { int x; int y; };

constexpr bool test() {
    std::string json;
    JsonFusion::Serialize(
        std::vector<Point>{ {1, 2}, {3, 4} }, json);
    return json == R"([{"x":1,"y":2},{"x":3,"y":4}])";
}

static_assert(test());

int main() { return test() ? 0: 1; }
```

## Nested Structures

Structs can contain other structs. JsonFusion handles nesting automatically:

```cpp live check-only
#include <JsonFusion/parser.hpp>

struct Address {
    std::string street;
    std::string city;
};

struct Person {
    std::string name;
    int age;
    Address address;
};

constexpr bool test() {
    Person p;
    JsonFusion::Parse(p, R"({
        "name": "Alice",
        "age": 30,
        "address": {"street": "123 Main St", "city": "Springfield"}
    })");
    return p.name == "Alice"
        && p.age == 30
        && p.address.city == "Springfield";
}

static_assert(test());

int main() { return test() ? 0: 1; }
```


## What's Next

- [Modeling JSON with C++ Types](02-modeling-json-types.md) — the six JSON types, containers, and composition
- [Validation & Annotations](03-validation-and-annotations.md) — constraints, `A<>` wrapper, external annotations
- [Error Handling](04-error-handling.md) — ParseResult, JSON paths, diagnostics
- [Streaming](05-streaming.md) — count canada.json primitives without loading the whole file into memory.
- Read [Why Reflection?](../WHY_REFLECTION.md) to understand the design

[Back to Documentation](../index.md)
