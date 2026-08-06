# JsonFusion
[![Docs](https://img.shields.io/badge/docs-GitHub%20Pages-blue?logo=github&logoColor=white&link=https://tucher.github.io/JsonFusion/)](https://tucher.github.io/JsonFusion/)
[![Constexpr Tests](https://github.com/tucher/JsonFusion/actions/workflows/constexpr_tests.yml/badge.svg)](https://github.com/tucher/JsonFusion/actions/workflows/constexpr_tests.yml)
[![Embedded code size benchmarks](https://github.com/tucher/JsonFusion/actions/workflows/embedded_code_size_benchmarks.yml/badge.svg)](https://github.com/tucher/JsonFusion/actions/workflows/embedded_code_size_benchmarks.yml)

Parse JSON directly into your structs with validation and no glue code.


#### Type-driven JSON parser/serializer with declarative validation: define your types, compiler optimizes for your target—fast on servers, compact on MCUs. Zero boilerplate, zero tuning, same code everywhere.

Your C++ types are the schema.
JsonFusion generates a specialized parser for them at compile time.

## Motivating example

```cpp
#include <JsonFusion/parser.hpp>
#include <JsonFusion/serializer.hpp>

using JsonFusion::A;

struct Config {
    struct Network {
        std::string                name;
        std::string                address;

        A<int, range<8000, 9000> > port;
/*      ↑ more on this later ↑       */
    };

    Network network;

    struct Drive {
        int   id;
        float max_speed;
        bool  active;
    };
    std::vector<Drive> drives;
};

Config conf;

std::string input = R"({"drives":[{"id":0,"max_speed":4.1,"active":true}],
                    "network":{"name":"1","address":"0.0.0.0","port":8081}})";

auto parseResult = JsonFusion::Parse(conf, input);
// error checking omitted for brevity

std::string output;
JsonFusion::Serialize(conf, output);

```
*No registration macros , no JSON DOM, no inheritance*

| JSON Type | C++ Type                                                       |
|-----------|----------------------------------------------------------------|
| object    | `struct`,  `std::map<string, Any>` *streamers*                 |
| array     | `std::vector<...>`, `std::array<...>`, *streamers*             |
| null      | `std::optional<...>`, `std::unique_ptr<...>`                   |
| string    | `std::string`, `std::vector<char>`, `std::array<char, N>`, ... |
| number    | `int`*(all kinds)*, `float`, `double`                          |
| bool      | `bool`                                                         |


## Table of Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [Main Features](#main-features)
- [Design Goals (and Tradeoffs)](#design-goals-and-tradeoffs)
- [Zero Runtime Subsystem](#zero-runtime-subsystem)
- [Types as Performance Hints](#types-as-performance-hints)
- [Positioning](#positioning)
  - [No Extra "Mapping" Layer](#no-extra-handwritten-mappingvalidation-layer-with-competitive-performance)
  - [You Own Your Containers](#you-own-your-containers)
  - [Embedded-Friendliness](#embedded-friendliness)
  - [C Interoperability](#c-interoperability)
  - [Related Work / Comparison](#related-work--comparison)
- [Declarative Schema and Runtime Validation](#declarative-schema-and-runtime-validation)
  - [Supported Options](#supported-options-include)
- [CBOR Support](#cbor-support)
- [Benchmarks](#benchmarks)
- [Custom Types & Transformers](#custom-types--transformers)
- [Advanced Features](#advanced-features)
  - [Optional High-Performance yyjson Backend](#optional-high-performance-yyjson-backend)
  - [Constexpr Parsing & Serialization](#constexpr-parsing--serialization)
  - [Compile-Time Size Estimation](#compile-time-size-estimation)
  - [Streaming Producers & Consumers (Typed SAX)](#streaming-producers--consumers-typed-sax)
  - [Compile-Time Testing](#compile-time-testing)
  - [JSON Schema Generation](#json-schema-generation)
- [Limitations and When NOT to Use](#limitations-and-when-not-to-use)


## Installation

JsonFusion is a **header-only library**. Simply copy the `include/` directory into your project's include path 
(e.g. so `#include <JsonFusion/parser.hpp>` works and `pfr.hpp` is visible).

Package managers: [Conan](docs/conan_usage.md), [vcpkg](docs/vcpkg_usage.md), [PlatformIO](docs/PLATFORMIO.md)

### Requirements

- **C++23** or later
- **Compiler: GCC 14+ or Clang 20+** (other compilers not currently supported). The optional C++26 reflection mode requires **GCC 16+** (`-std=c++26 -freflection`).
- **Boost.PFR** (bundled into `/include`, no separate installation needed)

## Quick Start

Try JsonFusion in 30 seconds:

```bash
git clone https://github.com/tucher/JsonFusion.git
cd JsonFusion
g++ -std=c++23 -I./include examples/basic_usage.cpp -o basic_usage && ./basic_usage
```

**Output:**
```
Successfully parsed!
App: MyApp
Version: 1
Debug: ON
Server: localhost:8080
```

📁 **See the code:** [`examples/basic_usage.cpp`](examples/basic_usage.cpp)  

## Main features

- **No glue code**: The main motivation behind the design is to make working with JSON similar to 
how it is usually done in Python, Java, Go, etc..
- **Competitive performance**: Matches or exceeds RapidJSON + hand-written mapping in real-world parse-validate-populate 
 workflows on realistic data (see [Benchmarks](#benchmarks)). What would take hundreds of 
 lines of manual mapping/validation code collapses into a single `Parse()` call—you just define your structs 
 (which you'd need anyway) and JsonFusion handles the rest. On structurally complex payloads like twitter.json JsonFusion is slightly faster than reflect-cpp, another zero-boilerplate reflection-based library.
- **Competitive binary footprint**: On embedded ARM (Cortex-M7 and Cortex-M0+) and on Esp32, on typical application setup with `-Os` JsonFusion matches or beats popular json embedded libraries while maintaining modern C++23 type safety and declarative validation. 
- The implementation conforms to the JSON standard (including arbitrary field order in objects)
- Validation of JSON shape and structure, field types compatibility and schema, all done in a single parsing pass
- No macros, no codegen, no registration – relies on PFR-driven introspection (or C++26 native reflection on GCC 16+)
- Works with deeply nested structs, arrays, strings, and arithmetic types out of the box
- No data-driven recursion in the parser: recursion depth is bounded by your C++ type nesting, not by JSON depth. With only
 fixed-size containers, there is no unbounded stack growth.
- **Rich error reporting**: Diagnostics with JSON path tracking (e.g., `$.statuses[3].user.name`),
input iterator position, parse error codes, and validator error codes with failed constraint details. Path tracking uses
compile-time sized storage based on schema depth analysis (zero runtime allocation overhead). Works in both runtime
and constexpr contexts. Cyclic recursive types can opt into dynamic path tracking via macro configuration. [Docs](docs/how-to/ERROR_HANDLING.md)
- **CBOR support** with the same guarantees 
- **Escape hatches**: `WireSink<>` core type to capture raw unparsed fragments when structure is unknown at compile time (plugins, pass-through, deferred parsing, schema algebra implementations).
JsonFusion validates protocol correctness while preserving the original fragment as a string, bridging typed and untyped worlds when needed.

## Design Goals (and Tradeoffs)

- **Single source of truth**: Your C++ types are the schema (no separate IDL/codegen).

- **One-pass, fused work**: Parse, validate, and populate structs in a single pass—no intermediate DOM, no post-processing.

- **Predictable footprint**: Template-heavy design structured to avoid code-size explosion across multiple models with both `-O3` and `-Os`. Shared infrastructure prevents duplication.

- **No hidden runtime knobs**: Behavior is controlled by types and annotations, not global flags.

- **Three layers only**:
  1. JSON structure ↔ C++ types
  2. Constraints/validators on those types
  3. Transformers/streamers to integrate JSON with your domain logic

## Zero Runtime Subsystem

JsonFusion doesn’t ship a runtime “engine”. All schema plumbing — validators, transformers, options, streamers — is resolved at compile time and either:
- turns into a couple of plain branches, or
- disappears entirely if unused.

### What actually ends up in your binary

The only runtime pieces you pay for are:

- Basic C/C++ primitives  
  `memcpy`, `memmove`, `memcmp`, integer ops, plus a few floating-point helpers from libgcc/libm (e.g. `__aeabi_dadd` on ARM).

- Whatever *your model* chooses to use  
  `std::vector`, `std::string`, `std::map`, `std::optional`, etc. If it’s in your struct, the compiler will pull it in. If it isn’t, JsonFusion doesn’t.

### What JsonFusion itself uses

Internally the core is almost entirely compile-time:

- Tiny, value-like types:  
  `std::array`, `std::bitset`, `std::optional`, `std::pair`, `std::tuple`
- Type machinery only:  
  `<type_traits>`, `<utility>`, `<limits>`, etc.

These are all header-only and generate trivial code; there’s no heavy runtime STL machinery in the core.

### What JsonFusion never drags in

JsonFusion does **not** depend on:

- Dynamic STL containers or algorithms (`std::unordered_map`, `std::set`, `<algorithm>` on big ranges)
- iostreams, threads, atomics, filesystem, chrono, locale, regex
- RTTI or exceptions (`-fno-exceptions -fno-rtti` is fully supported)
- Virtual bases or polymorphic hierarchies

If you see any of those in your binary, it came from your code, not from JsonFusion.

### One codebase, all targets

The same template-driven design is used everywhere — from ARM Cortex-M0/M7/Esp32 microcontrollers to x64 servers. There is no “lite” embedded fork:

- On big systems, the compiler inlines and folds the templates into tight loops over your model.
- On microcontrollers, the same code compiles down (under `-Os`/LTO) to a small, predictable blob with no hidden runtime subsystem.

The only real question is not *“how do I tune JsonFusion?”* but *“is this design a good fit for my application’s constraints?”*

## Types as Performance Hints

Your type definitions aren't just schema—they're compile-time instructions to the parser.

Assume you want to count primitives in GeoJSON data, but don't need actual values. With JsonFusion you model coordinates pair as
```cpp
struct Pt_ {
    A<float, skip> x;
    A<float, skip> y;
};
using Point = A<Pt_, as_array>;
```
- On canada.json (2.15 MB numeric-heavy GeoJSON), a hand-written RapidJSON SAX handler that counts 
features/rings/points serves as the baseline.
- JsonFusion Streaming with selective skipping is **~60% faster** simply because the type system tells the parser "these values exist, 
but we don't need them." By annotating coordinate fields with `skip`, JsonFusion skips float parsing entirely while still 
validating JSON structure.

RapidJSON-like APIs have no way to express that intent without custom low-level parsing code; JsonFusion does it 
with a single annotation. The same declarative type system that eliminates boilerplate also exposes 
high-level control over low-level optimizations.

## Positioning

### No extra handwritten mapping/validation layer, with competitive performance

Traditional setups use a fast JSON parser (RapidJSON, simdjson, etc.) and then a second layer of hand-written 
mapping into your C++ types. JsonFusion fuses parsing, mapping, and validation into a single pass 
— without the manual glue code.

You would have to do this work anyway; JsonFusion just helps automate it. So it doesn’t just optimize raw string
parsing; it optimizes development effort, debugging time, and lowers the maintenance burden of endless 
boilerplate that just moves bytes around.

You can think of it as a code generator that automatically produces a good parser/serializer tailored to your models.

### You own your containers

JsonFusion never dictates what containers or strings you must use. It works with any type that behaves like a 
standard C++ container:

- strings: `std::string`, `std::array<char, N>`, your own fixed buffers…
- arrays/lists: `std::vector<T>`, `std::list<T>`, `std::array<T, N>`, etc.

JsonFusion does **not** install custom allocators, memory pools, or arenas. It simply reads JSON and writes 
into the storage you provide. 

This has a few consequences:

- You keep full control over memory behavior (heap vs stack, fixed-size vs dynamic).
- Integration with existing C++ and even C structs is straightforward.
- There are no hidden allocation tricks inside the library.
- There are very few global options/knobs-not needed.

It also explains part of the performance story: libraries like RapidJSON use highly tuned internal DOM structures and custom allocators, which can squeeze
out more speed for dynamic, DOM-heavy workloads. JsonFusion chooses instead to be a thin, strongly typed layer over *your* containers, while still staying
competitive in raw parsing speed.

### Embedded-friendliness

No data-driven recursion in the parser core — depth is bounded by your user-defined types and is explicit.

No dynamic allocations inside the library. You choose your storage: `std::array`, fixed-size char buffers, or dynamic containers (`std`-compatible).

Works with forward-only input/output iterators: you can parse from a stream or byte-by-byte without building a big buffer first, and serialize
byte-by-byte with full control and no intermediate buffers.

Plays well with -fno-exceptions / -fno-rtti style builds.

**No external dependencies**: By default uses in-house  float parser/serializer.

**Your structs define everything**: On tiny microcontrollers without floating-point support, just don’t use float/double in your struct definitions. 
In that case the float parsing code is never instantiated or compiled into the binary; there is no special global configuration needed. 
The same applies to dynamic lists/strings.

### C Interoperability

Add JSON parsing to legacy C codebases **without modifying your C headers**. JsonFusion works with plain C structs using external annotations 
in a separate C++ translation unit:

```c
// structures.h - Pure C header, unchanged
typedef struct {
    int64_t position[3];  // C arrays fully supported
    int active;
    char name[20];        // Auto null-terminated
} Motor;

typedef struct {
    Motor primary_motor;
    Motor motors[5];      // Nested C arrays supported
    int motor_count;
    char system_name[32];
} MotorSystem;
```

```cpp
// parser.cpp - C++ wrapper with StructMeta annotations
template<>
struct JsonFusion::StructMeta<Motor> {
    using Fields = StructFields<
        Field<&Motor::position, "position", min_items<3>>,
        Field<&Motor::active, "active">,
        Field<&Motor::name, "name", min_length<1>>
    >;
};

extern "C" int ParseMotorSystem(MotorSystem* system, 
                                const char* json_data, 
                                size_t json_size) {
    auto result = JsonFusion::Parse(*system, json_data, json_data + json_size);
    return result ? 0 : static_cast<int>(result.error());
}
```

```c
// main.c - Pure C usage
MotorSystem system;
const char *json = "{ ... }";
size_t json_size = strlen(json);
int error = ParseMotorSystem(&system, json, strlen(json));
```

**3-Layered External Annotation System:**
1. **`Annotated<T>`** - Type-level options for generic types
2. **`AnnotatedField<T, Index>`** - Field-level options for PFR-introspectable structs
3. **`StructMeta<T>`** - Full manual control (required for C arrays, optional otherwise)

**String handling**: Fixed-size char arrays are automatically null-terminated, making them immediately usable with standard C string functions 
(`strlen`, `strcmp`, `printf`).

**C arrays**: Supported via `StructMeta` annotations. Single-dimensional arrays (`int arr[10]`) support full validation. 
Nested arrays (`double matrix[3][3]`) work for serialization and round-trip parsing, but annotations apply only to the outermost level.

Perfect for:
- **Legacy firmware** - Add JSON without touching existing C headers
- **Embedded C projects** - Modern JSON parsing with type safety and validation
- **Mixed C/C++ codebases** - Keep C structs pure, JSON metadata in C++

📁 **Complete example with round-trip tests**: [`examples/c_interop/`](examples/c_interop/)

### Related Work / Comparison

**JsonFusion vs Glaze vs reflect-cpp**

JsonFusion sits next to Glaze and reflect-cpp in the "typed C++ ↔ JSON" space, but with different priorities.

**Glaze**
- Typically significantly faster than JsonFusion on flat, in-memory JSON: its tokenizer is tuned for contiguous buffers and low-level optimizations.
- Glaze builds on a hand-rolled JSON scanner that works directly on `char*` with extensive inlining. It assumes
contiguous buffers and is not iterator-generic or streaming-friendly in the same way JsonFusion is.
- It focuses on "shape-driven" deserialization: once key dispatch is resolved (often via generated lookup tables / perfect hashing), it writes straight
into fields with very little per-element overhead. That's a big part of why it's so fast on fixed, known schemas.
- **Embedded code size**: With proper embedded configuration, Glaze behaves similarly to JsonFusion regarding code size (tested from 2 to 20+ C++ models). Note: Glaze requires special configuration for maximum code size optimization: `#define GLZ_DISABLE_ALWAYS_INLINE` and using `glz::opts_size{}` (see `benchmarks/embedded/code_size/parse_config_glaze.cpp`).
- **Error reporting approach**: Glaze reports errors with line/column information and text context:
  ```
  1:17: expected_comma
     {"Hello":"World"x, "color": "red"}
                  ^
  ```
  JsonFusion provides structural JSON path tracking (e.g., `$.users[3].address.street: type mismatch`) in addition to buffer position. The choice depends on whether you prioritize text-oriented debugging or structural position within the JSON hierarchy.
- **Floating-point handling**: Glaze provides precise floating-point parsing with full round-trip guarantees. JsonFusion's default in-house FP parser/serializer is simpler and sufficient for typical use cases, but does not guarantee perfect round-trip precision (see [Limitations](#limitations-and-when-not-to-use) for details on alternative FP backends).
- Its design is heavily optimized around single-pass parses into C++ types with contiguous input and no dynamic "byte-by-byte" streaming; JsonFusion
trades some of that peak speed to support generic iterators, and streaming while staying fully header-only and constexpr-friendly.
- Uses its own metadata/registration style rather than "the types are the schema + inline annotations" like `A<T, opts...>` in JsonFusion.

**reflect-cpp**
- Performance is broadly comparable to **JsonFusion with default generic forward-only iterator** on typical object graphs.
- Focuses on being a general reflection + multi-format serialization layer (JSON via yyjson, etc.).
- **Architectural difference**: reflect-cpp uses a [two-pass approach](https://github.com/getml/reflect-cpp/blob/main/include/rfl/json/read.hpp)—first
parsing JSON into a DOM tree (yyjson), then mapping the DOM to C++ types. JsonFusion parses directly from input iterators into your structs in a single
pass. This means reflect-cpp requires the full JSON in memory and allocates an intermediate DOM tree, while JsonFusion works with forward-only iterators
and has no hidden allocations beyond your own containers.
- JsonFusion is narrower but deeper: header-only, no hidden allocations, with schema-attached validation, skipping, streaming,
and rich error contexts.
- JsonFusion offers first-class streaming / forward-iterator parsing and compile-time constraints; reflect-cpp focuses more on "reflect types,
then hand them to fast runtime backends."
- **Metadata syntax**: JsonFusion uses a uniform flat list of type parameters (`A<string, key<"userName">, min_length<1>, max_length<100>>`), while
reflect-cpp uses nested composition (`rfl::Rename<"userName", rfl::Validator<string, rfl::Size<rfl::Minimum<1>, rfl::Maximum<100>>>>`). JsonFusion's
approach keeps all metadata co-located in a single, composable type list—simpler to read and extending naturally to validators, options, and custom
constraints without changing syntax patterns.

In short: JsonFusion in its default mode trades some maximum GB/s for a strongly typed, constexpr-driven, streaming-friendly design with uniform metadata
composition; 


## Declarative Schema and Runtime Validation

Declarative compile-time schema and options, runtime validation in the same parsing single pass.
Turn your C++ structs into a static schema, decouple variable names from JSON keys, and add parsing rules by annotating fields.

**Full reference:** [Annotations Reference](docs/ANNOTATIONS_REFERENCE.md)

```cpp
using JsonFusion::A;
using namespace JsonFusion::options;
using std::string; using std::vector, std::optional;
struct GPSTrack {
    A<int,
        key<"id">,
        range<1, 8>>             more_convenient_descriptive_id;

    A<string,
        min_length<1>,
        max_length<32>>          name;

    A<bool, exclude>    m_is_handled;

    struct Point {
        float x;
        float y;
        float z;
    };
    A<std::vector<
            A<Point, as_array>>,
        max_items<4096>>         points;
};

list<optional<GPSTrack>> tracks;
JsonFusion::Parse(tracks, string_view(R"JSON(
    [
        null,
        {"name": "track1", "id": 1, "points": [[1,2,3], [4,5,6], [7,8,9]]},
        null
    ]
)JSON"));
```

### Behaviour of Annotated<> (has A<> alias) class
Tiny wrapper around your types, without inheritance, but with some glue to make it work as "natural" as possible.

```cpp
auto handlePoint = [](const GPSTrack::Point & p){};
for (const auto & track: tracks) {
    if(!track) continue;
    for (const auto & point: track->points) { // implicit conversions & operator forwarding
        handlePoint(point);
    }
}
```

**External Annotations**: If you need to keep your types pure and clean, there are external annotations too, which work on 3 levels:

```cpp
// 1. For generic types - type-level annotations
template<> struct JsonFusion::Annotated<T> {
    using Options = OptionsPack< /*Same as for inline ones*/>;
};

// 2. For PFR-introspectible structs - field-level annotations, attached to N-th field
template<> struct JsonFusion::AnnotatedField<T, Index> {
    using Options = OptionsPack< /*Same as for inline annotated structs*/ ...>;
};

// 3. Generic manual annotation, which should work even for C arrays in C structs. Overrides the PFR automatic introspection completely
template<>
struct JsonFusion::StructMeta<T> {
    using Fields = Fields<
        Field<&T::member, "/*name goes here*/", /*annotations go here*/ ... >,        
        ...
    >;
};
```

This is particularly useful for third-party types, C interop, legacy code, or when you want to keep JSON metadata separate from your business logic types.
See [`examples/external_meta.cpp`](examples/external_meta.cpp) for a complete example.

### Supported Options Include

JsonFusion provides validators (runtime constraints) and options (metadata/behavior control):

- **Validators**: `range<>`, `min_length<>`, `max_length<>`, `enum_values<>`, `min_items<>`, `max_items<>`, `constant<>`, and more
- **Options**: `key<>`, `exclude`, `skip`, `as_array`, `allow_excess_fields`, and more

See the complete reference: [Annotations Reference](docs/ANNOTATIONS_REFERENCE.md)

### Custom Validators

JsonFusion's validator system is **event-driven**. Built-in validators like `range<>`, `min_length<>` are attached to specific parsing events (e.g.,
`number_parsing_finished`, `string_parsing_finished`). You can extend this system by binding **stateless lambdas or free functions** to validation events
using `fn_validator<Event, Callable>`.

Different events have different argument signatures matching the parsed data and parsing context at that stage.

**Example: Divisibility validator**

```cpp
using JsonFusion::validators_detail::parsing_events_tags::number_parsing_finished;

struct Config {
    // Only accept numbers divisible by 10
    A<int, fn_validator<number_parsing_finished, [](const int& v) { 
        return v % 10 == 0; 
    }>> port;
};

Parse(config, R"({"port": 8080})");  // ✅ Passes: 8080 % 10 == 0
Parse(config, R"({"port": 8081})");  // ❌ Fails: validation error
```
Also very simple to build reusable types like this:
```cpp
template <std::uint64_t D>
using IntDivisibleBy =
    A<int, fn_validator<number_parsing_finished, [](const int & v){ return v % D == 0; }>>;

```

📁 **See also:** [Annotations Reference - Custom Validators](docs/ANNOTATIONS_REFERENCE.md#generic-custom-validators) for complete event list and
signatures.


## CBOR Support

JsonFusion supports **CBOR (RFC 8949)** with the same guarantees as JSON:
- ✅ Forward-only, byte-by-byte streaming (no seek, no buffering)
- ✅ Zero runtime, zero allocations
- ✅ Same features: validation, transformers, streaming, constexpr

Examples could be found in  [`code size benchmarks`](benchmarks/embedded/code_size/parse_config_cbor.cpp) 

**Usage:**

```cpp
#include <JsonFusion/cbor.hpp>

// Parsing
Config config;
auto result = JsonFusion::ParseWithReader(config, JsonFusion::CborReader(begin, end));

// Serialization
JsonFusion::SerializeWithWriter(config, JsonFusion::CborWriter(out_begin, out_end));
```

## Benchmarks

JsonFusion targets two distinct scenarios with different priorities:

- **Embedded systems** – where binary size is critical (measured in KB)
- **High-performance systems** – where throughput is critical (measured in µs)

**Performance philosophy**: JsonFusion tries to achieve both speed and compactness by 
*eliminating unnecessary work*, rather than through manual micro-optimizations. 
The core is platform/CPU agnostic (no SIMD, no hand-tuned assembly). 
With the default `JSONFUSION_FP_BACKEND=0`, there are no explicit runtime dependencies of the library itself.

Less work means both faster execution *and* smaller binaries.
It is all about avoiding doing the same work multiple times.

JsonFusion leverages **compile-time reflection** through Boost.PFR (or C++26 native reflection on GCC 16+), enabling the compiler to know everything about your types before runtime. C++26 reflection support is already implemented and tested with GCC 16 (`-std=c++26 -freflection`), providing zero-dependency introspection and native `[[=A<...>{}]]` annotation syntax as an alternative to `Annotated<T, ...>` wrappers.

### Binary Size (Embedded Focus)

JsonFusion is benchmarked on multiple embedded platforms: **ARM Cortex-M7/M0+** (STM32, SAMV7) and **ESP32** (Xtensa LX6). All tests use a realistic workload: parsing and serializing **two distinct JSON message types**—a configuration object and an RPC command structure—with nested objects, arrays, validation constraints, and optional fields.

**What we're measuring:** Complete workflow for **two models**: **parse JSON + populate structs + validate constraints + serialize again**. This simulates real embedded systems that handle multiple message types (config, commands, telemetry, etc.).

📁 **Benchmark**: [`benchmarks/embedded/code_size/`](benchmarks/embedded/code_size/)  
📁 **Models**: [`embedded_config.hpp`](benchmarks/embedded/code_size/embedded_config.hpp) (EmbeddedConfig + RpcCommand)

---

#### ARM Cortex-M (STM32, SAMV7, etc.)

**Test Setup:**
- **Compiler**: `arm-none-eabi-gcc 14.2.1`
- **Targets**: ARM Cortex-M7 (`-mcpu=cortex-m7 -mthumb`) and Cortex-M0+ (`-mcpu=cortex-m0plus -mthumb`)
- **Compilation**: `-Os -fno-exceptions -fno-rtti -fno-threadsafe-statics -ffunction-sections -fdata-sections -DNDEBUG -flto -Wall` (zero warnings)
- **Linking**: `-specs=nano.specs -specs=nosys.specs -Wl,--gc-sections -flto`

**TL;DR:** ✅ **JsonFusion is one of the smallest on Cortex-M0+ and Cortex-M7 on -Os build** — while eliminating manual boilerplate and adding declarative validation.

**Results (`.text` section - code size in flash):**

| Library                               |     M7    |    M0+    |  Version   |
|---------------------------------------|-----------|-----------|------------|
| **JsonFusion CBOR**                   |  17.0 KB |  24.2 KB | dce75cf3   |
| ArduinoJson                           |  20.0 KB |  29.2 KB | v7.4.3     |
| **JsonFusion**                        |  21.8 KB |  31.7 KB | dce75cf3   |
| cJSON                                 |  25.4 KB |  35.2 KB | fb16e5cf   |
| yajl                                  |  28.3 KB |  37.5 KB | 5e3a7856   |
| Glaze(with embedded-friendly config)  |  30.4 KB |  36.9 KB | ba2a514b   |

**Key Takeaways:**

1. **JsonFusion with `-Os` is relatively small on both M7 and M0+**  while ArduinoJson and cJSON require **hundreds of lines of manual, error-prone boilerplate** (type-unsafe field access, manual validation, manual error handling)

2. **CBOR support is very compact:** JsonFusion's CBOR implementation (parse + serialize) adds minimal overhead compared to JSON parsing only—providing full bidirectional binary protocol support with the same type-safe API.

---

#### ESP32 (Xtensa LX6)

**Test Setup:**
- **Compiler**: `xtensa-esp-elf-gcc 14.2.0`
- **Target**: ESP32 (Xtensa LX6 architecture)
- **Compilation**: `-Os -fno-exceptions -fno-rtti -fno-threadsafe-statics -ffunction-sections -fdata-sections -DNDEBUG -flto -mlongcalls -mtext-section-literals`
- **Linking**: `-Wl,--gc-sections -flto`

**TL;DR:** ✅ **JsonFusion is one of the smallest on ESP32**

**Results (`.text` section - code size in flash):**

| Library                               |   ESP32   |  Version   |
|---------------------------------------|-----------|------------|
| **JsonFusion CBOR**                   |  20.3 KB | dce75cf3   |
| ArduinoJson                           |  25.1 KB | v7.4.3     |
| **JsonFusion**                        |  26.1 KB | dce75cf3   |
| Glaze(with embedded-friendly config)  |  35.0 KB | ba2a514b   |
| yajl                                  |  58.9 KB | 5e3a7856   |
| cJSON                                 |  65.1 KB | fb16e5cf   |

**Key Takeaways:**

1. **Cross-platform consistency:** JsonFusion maintains similar code size across different architectures (ARM M7, ESP32, ARM M0+), with the size varying primarily based on the target's instruction set density rather than the library's architecture-specific code.

2. **CBOR overhead remains reasonable:** Full CBOR parse + serialize support adds minimal overhead compared to JSON-parse-only.

---

#### Bonus: 8-bit AVR Support

JsonFusion **compiles for AVR ATmega2560** (8-bit Arduino) **without any code changes**, though this is an exotic setup requiring [modern avr-gcc 15](https://github.com/ZakKemble/avr-gcc-build) and [non-standard avr-libstdcpp](https://github.com/modm-io/avr-libstdcpp). 

**AVR Results (`.text` section - code size in flash)**, same benchmarks:

| Library         |             |
|-----------------|-------------|
| jsmn            |   10.2 KB   |
| cJSON           |    9.1 KB   |
| ArduinoJson     |   20.7 KB   |
| **JsonFusion**  | **25.8 KB** |

On 8-bit AVR, JsonFusion's generic code incurs overhead compared to minimal C parsers, but remains competitive with ArduinoJson. The value proposition: modern C++23 with type safety and the same codebase that runs on 8-bit AVR and 64-bit servers, versus manual parsing.


### Parsing Speed (High-Performance Focus)

**What we're measuring:** The complete real-world workflow of parsing JSON and populating C++ structs with validation. This is what you actually do in 
production code—not abstract JSON DOM manipulation or raw string parsing in isolation.

Both libraries perform the same work:
1. Parse JSON from string
2. Validate all constraints (type compatibility, ranges, array sizes, string lengths, enum values)
3. Populate C++ structures with the data

For RapidJSON, this requires hand-written mapping code and hand-written checks. For JsonFusion, it's automatic via reflection.

📁 **Test models**: [`benchmarks/main.cpp`](benchmarks/main.cpp) – 6 realistic scenarios with nested structs, arrays, maps, optionals, validation
constraints  


#### Results

| Benchmark Scenario | JsonFusion vs RapidJSON |
|-------------------|-------------------------|
| **Embedded Config (Static)** | **~same speed** |
| **Embedded Config (Dynamic)** | **40% faster** |
| **Telemetry Samples** | **20% faster** |
| **RPC Commands** | **65% faster** |
| **Log Events** | **15% faster** |
| **Bus Events / Message Payloads** | **20% faster** |
| **Metrics / Time-Series** | **15% faster** |

*Tested on Apple M1 Max, macOS 26.1, GCC 14, 1M iterations per scenario. RapidJSON uses DOM + manual populate (typical), or hand-written SAX for static
embedded config (optimal).*

#### Key Takeaways

**1. Competitive with hand-optimized code**

The **Embedded Config (Static)** benchmark uses RapidJSON's SAX parser with a hand-written state machine—the most performance-oriented approach possible.
JsonFusion matches this performance despite using a generic reflection-based approach that requires **zero mapping code**, eliminating hundreds of
lines of error-prone boilerplate.

You get the same speed as hand-optimized SAX parsers on trivial static cases, plus compile-time type safety, declarative validation, and zero maintenance burden. On
dynamic containers, JsonFusion is significantly faster while still requiring zero code.

**2. Wins where it matters**

JsonFusion is generally faster than RapidJSON + manual code on realistic workloads.

**3. Productivity vs peak performance**

JsonFusion trades a small loss on trivial static cases for massive productivity gains:
- **Zero boilerplate**: No manual DOM traversal, type conversion, or validation code
- **Single source of truth**: Your C++ types are the schema
- **Compile-time safety**: Catch errors at compile time, not runtime
- **Maintainability**: Changes to models automatically update parsing logic

**4. Fair comparison**

RapidJSON benchmarks use DOM + manual populate (typical usage) or hand-written SAX state machines (embedded static case). Both do the same work as
JsonFusion: parse, validate, populate. This is an apples-to-apples comparison of complete workflows, not raw parsing speed.

**5. Large file performance: canada.json (2.15 MB, 117K+ coordinates)**

The canada.json benchmark tests numeric-heavy GeoJSON—a pure array/number stress test.

**Parse-and-populate** (production use case):
- JsonFusion is ~15% slower than RapidJSON (with manual populate)

**Streaming for object counting**:
- Hand-written RapidJSON SAX serves as baseline
- JsonFusion typed streaming is ~80% slower, but provides **fully generic, type-safe API** that handles complex schemas the same way as simple ones—no
manual state machines
- **JsonFusion with selective skipping: ~60% faster than hand-written RapidJSON SAX** by declaring unneeded values as `skip` in the model

The typed streaming approach trades some speed on trivial regular data for universal composability. Where RapidJSON SAX requires custom code per schema,
JsonFusion's declarative skipping works uniformly across any model.

📁 **Canada.json benchmark**: [`benchmarks/canada_json_parsing.cpp`](benchmarks/canada_json_parsing.cpp)

**6. Structured data performance: twitter.json (0.6 MB, 100 status objects)**

The twitter.json benchmark tests realistic deeply nested objects with optionals and mixed types—representative of real-world REST API responses.

**C++ Library Comparison:**

JsonFusion is:
- **slightly faster than reflect-cpp** (but JsonFusion does not use 2-pass approach with yyjson and DOM building)
- **About 1.5× faster than RapidJSON + manual populate** (eliminates 450+ lines of hand-written mapping code)
- **However, Glaze is ~3× faster than JsonFusion** - Glaze's hand-tuned, contiguous-buffer-optimized design achieves exceptional raw speed by
trading off some streaming flexibility, generic iterators, and path-based errors-handling. Or it is just written better😄


**Cross-Language Comparison:**

For perspective, managed languages on the same benchmark (.NET 9.0.112, OpenJDK 21.0.9):
- **Java DSL-JSON** (compile-time code generation): ~950 µs — competitive with several C++ approaches! Shows that compile-time generation
delivers near-native performance even in managed languages.
- **C# System.Text.Json** (source generation + JIT): ~1600 µs — in the same ballpark as RapidJSON + manual C++.

This demonstrates that modern managed runtimes can be competitive, especially with compile-time code generation. However, optimized C++ still provides
advantages: Glaze is 2.5× faster than Java DSL-JSON.

**Performance Summary (twitter.json, parse + populate + validate, same arm64 Ubuntu Linux 24.04 machine, GCC 14.3):**

| Library | Language | Approach | Time (µs/iter) |
|---------|----------|----------|----------------|
| Glaze | C++ | Default, no external deps | 360 |
| **Java DSL-JSON** | **Java** | **Compile-time generation** | **950** |
| JsonFusion          | C++ | Default, no external deps | 1020 |
| reflect-cpp + yyjson | C++ | DOM-based, two-pass | 1130 |
| **C# System.Text.Json** | **C#** | **Source generation + JIT** | **1600** |
| RapidJSON + manual | C++ | DOM + hand-written mapping | 1620 |

📁 **C++ benchmark**: [`benchmarks/twitter_json/`](benchmarks/twitter_json/)  
📁 **Java benchmark**: [`benchmarks/twitter_json_java/`](benchmarks/twitter_json_java/)  
📁 **C# benchmark**: [`benchmarks/twitter_json_c_sharp/`](benchmarks/twitter_json_c_sharp/)  
📁 **Data model**: [`benchmarks/twitter_json/twitter_model_generic.hpp`](benchmarks/twitter_json/twitter_model_generic.hpp) 

## Custom Types & Transformers

JsonFusion doesn't include built-in support for application-specific types like dates, UUIDs, or currencies, as well as high-level JSON schema algebra. Instead, it provides **generic transformation hooks** that let you define custom conversions between JSON representations and your domain types.

Internally, JsonFusion thinks in three layers:

1. **Native JSON layer** – objects/arrays/strings/numbers/bools/null mapped to C++ types.
2. **Constraint layer** – validators (`range<>`, `min_length<>`, etc.) that restrict the valid subset of JSON for that type.
3. **Domain layer** – transformers and streamers that turn JSON-shaped data into domain types (dates, IDs, aggregates) or side effects.

📖 **Deep dive**: [docs/JSON_SCHEMA.md](docs/JSON_SCHEMA.md) - JSON type mapping and why no first-class `std::variant`

**Example: ISO Date String → Microseconds**

```cpp
#include <JsonFusion/parser.hpp>
#include <JsonFusion/serializer.hpp>
#include <JsonFusion/generic_transformers.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>

// Parse "2024-12-31T23:59:59Z" → microseconds since epoch
bool parse_iso_date(int64_t& usec, const std::string& iso);

// Microseconds since epoch → "2024-12-31T23:59:59Z"
bool format_iso_date(const int64_t& usec, std::string& iso);

// Define the transformer
using Timestamp = JsonFusion::transformers::Transformed<
    int64_t,              // Store as microseconds
    std::string,          // Wire format is ISO string
    parse_iso_date,       // Parse function
    format_iso_date       // Serialize function
>;

struct Event {
    A<Timestamp,     key<"timestamp">> timestamp_us;
    std::string name;
};

// Usage
Event event;
auto result = JsonFusion::Parse(event, R"({
    "timestamp": "2024-12-31T23:59:59Z",
    "name": "New Year"
})");

// Access: event.timestamp.value is int64_t (microseconds)
std::cout << "Microseconds: " << event.timestamp_us->value << "\n";

// Serialization converts back to ISO string
std::string json;
JsonFusion::Serialize(event, json);
// {"timestamp":"2024-12-31T23:59:59Z","name":"New Year"}
```

**Key features:**
- **Fully composable**: Transformers work with arrays, optionals, nested structs, etc.
- **Bidirectional**: Single definition handles both parsing and serialization
- **Zero-overhead**: All transformations resolve at compile time
- **Type-safe**: Errors caught during compilation

**Schema evolution:** Transformers can handle type migrations (e.g., `bool` → `int`) in a generic, reusable way by capturing raw JSON with `WireSink<>` and trying multiple parse attempts. See [`examples/schema_evolution_bool_to_enum.cpp`](examples/schema_evolution_bool_to_enum.cpp) for example.

📖 **Full guide**: [docs/TRANSFORMERS.md](docs/TRANSFORMERS.md) covers:
- Core concepts (`ParseTransformerLike`, `SerializeTransformerLike`)
- Building blocks (`Transformed`, `ArrayReduceField`, `MapReduceField`)
- Composition patterns
- Relationship with streaming interfaces

## Advanced Features

### Optional high-performance yyjson backend

⚠️ **IMPORTANT:** This is a **purely optional feature** for benchmarking and specific high-performance scenarios.  
**It is NOT recommended for general use** and does **not** preserve JsonFusion's core guarantees:
- ❌ No longer zero-allocation inside the library (yyjson allocates a DOM tree)
- ❌ No longer streaming/forward-only (requires entire JSON in memory)
- ❌ Adds external runtime dependency on yyjson

The core JsonFusion library remains header-only and zero-allocation internally;  
the yyjson backend is an opt-in integration layer on top of the yyjson C library.

---

JsonFusion's parser is built around a small "reader" concept, making the low-level JSON engine pluggable. For benchmarking purposes or when you
specifically need maximum throughput on "all JSON already in memory" workloads, you can drop in a `YyjsonReader` that adapts a `yyjson_doc` DOM to the
same interface:

```cpp
#include <JsonFusion/yyjson_reader.hpp>

TwitterData model;
auto res = JsonFusion::ParseWithReader(model, JsonFusion::YyjsonReader(input.data(), input.size()));
```

This pluggable design proves that the abstraction works in practice: same models, same annotations, same validation logic—just swap the low-level engine.
However, **you trade JsonFusion's design philosophy (streaming, zero-allocation, no dependencies) for raw speed**.

**Trade-offs summary:**

| Feature | Iterator Reader (Default) | yyjson Reader (Optional) |
|---------|--------------------------|-------------------------|
| **Allocations** | ✅ Zero inside library | ❌ yyjson allocates DOM |
| **Streaming** | ✅ Forward-only, byte-by-byte | ❌ Requires all JSON in memory |
| **Dependencies** | ✅ None | ❌ Requires yyjson library |
| **Input sources** | ✅ Any (files, sockets, etc.) | ❌ Contiguous memory only |


**Performance results:** The yyjson backend is primarily included to demonstrate the
reader abstraction works. The main boost is achieved on floating-points heavy data, because JsonFusion's small in-house parser is slower than those from many other libraries.

Another important application for DOM-based readers implementations are applications with complex schema algebra. `WireSink` for such readers can be implemented in a very efficient way (like in reference `YyjsonReader`), which allows to eliminate any actual reparsing of input data. Related to all kinds of Variant-like transformers, etc.


### Constexpr Parsing & Serialization

For models using compatible containers (`std::string`, `std::vector` are compatible) both
`Parse` and `Serialize` are fully `constexpr`-compatible, as long as your c++ standard library is modern enough. This enables compile-time JSON/CBOR
validation, zero-cost embedded configs, see the compile-time test suite [`tests/constexpr/*`](tests/constexpr) for 
examples with nested structs, arrays, and optionals.

### Compile-Time Size Estimation

For embedded systems or fixed-buffer scenarios, JsonFusion can calculate the **maximum serialized JSON size** at compile time:

```cpp
#include <JsonFusion/serialize_size_estimator.hpp>

struct Config {
    int id;
    bool active;
    std::array<char, 32> name;
};

// Calculate at compile time
constexpr std::size_t max_size = JsonFusion::size_estimator::EstimateMaxSerializedSize<Config>();
// Result: 61 bytes (conservative upper bound)

// Allocate exact buffer - no guessing, no overflow
std::array<char, max_size> buffer{};
auto result = JsonFusion::Serialize(config, buffer.data(), buffer.data() + buffer.size());
```

This is particularly useful for embedded targets where dynamic allocation is prohibited—you get a **compile-time guarantee** that your buffer is always sufficient.

### Streaming Producers & Consumers (Typed SAX)

Any JSON array field can be bound to either a **container** (`std::vector<T>`, `std::array<T,N>`) or a **streaming producer/consumer**.

#### Streaming Producers (for Serialization)

Instead of materializing all elements in memory, fill them on demand:

```cpp
struct Streamer {
    struct Vector { float x, y, z; };
    using value_type = A<Vector, as_array>;

    mutable int counter = 0;
    int count;

    void reset() const { counter = 0; }
    
    stream_read_result read(Vector & v) const {
        if (counter >= count) return stream_read_result::end;
        counter++;
        v = {42.0f + counter, 43.0f + counter, 44.0f + counter};
        return stream_read_result::value;
    }
};
static_assert(ProducingStreamerLike<Streamer>);

struct TopLevel {
    Streamer points_xyz;  // Will serialize as JSON array
};

TopLevel a;
a.points_xyz.count = 3;
std::string out;
Serialize(a, out);
// {"points_xyz":[[43,44,45],[44,45,46],[45,46,47]]}
```

JsonFusion holds a **single `value_type` buffer on the stack** and repeatedly calls `read()`. 
No intermediate containers, no hidden allocations.

**Composability**: Since `value_type` can be any JsonFusion-compatible value (primitives, structs, 

`Annotated<>`, even other streamers), you can nest producers arbitrarily:

```cpp
struct StreamerOuter {
    struct StreamerInner {
        using value_type = double;
        // ... fills doubles on demand
    };
    using value_type = StreamerInner;  // Stream of streamers!
    // ... fills  StreamerInner instances state on demand
};

Serialize(StreamerOuter{}, out);
// [[1],[1,2],[1,2,3],[1,2,3,4],...]  – 2D jagged array, zero materialized containers
```

#### Streaming Consumers (for Parsing)

Consume elements as they arrive, without storing the whole array:

```cpp
struct PointsConsumer {
    using value_type = Vector;

    void reset() { /* called at array start */ }
    
    bool consume(const Vector & point) {
        processPoint(point);  // handle element immediately
        return true;
    }
    
    bool finalize(bool success) { 
        return success;  // called after last element
    }
};
static_assert(ConsumingStreamerLike<PointsConsumer>);

struct TopLevel {
    A<PointsConsumer, key<"points">> consumer;
};

Parse(toplevel, R"({"points": [[1,2,3], [4,5,6], ...]})");
// consume() called for each element, no std::vector allocated
```

JsonFusion parses each element directly into a **stack-allocated `value_type` buffer**, calls `consume()`, then reuses the buffer for the next element.

#### Why This Is Different from Classic SAX

Classic SAX APIs (RapidJSON, simdjson) drive you with low-level events:
```
StartArray, Key("foo"), String("bar"), Int(42), EndObject, …
```
You manually maintain state and assemble typed objects yourself.

**JsonFusion's approach:**
- You declare `value_type` (using the same `Annotated<>` system, just as for normal fields)
- JsonFusion parses each element into a **fully-typed C++ object**
- Your callbacks receive complete, validated structures—not raw tokens
- The abstraction **composes naturally**—streamers can contain structs, which contain other streamers
- API to pass local (both at types and runtime level) typed context pointer.
No need to use any global variables/singletons
- Mappings streamers are also available, to stream key-value pairs in both directions.

**Unified mechanism:**
- **Producers**: `read()` fills elements on demand (serialization)
- **Consumers**: `consume()` receives fully-parsed elements (parsing) at the moment last input char of primitive parsed
- Library uses only a single buffer object of corresponding type; all dynamic memory is under your control

📁 **Complete examples**: [`tests/sax_demo.cpp`](tests/sax_demo.cpp) including nested streamer composition, 
[`benchmarks/canada_json_parsing.cpp`](benchmarks/canada_json_parsing.cpp) for Context Propagation.

#### High-Level Stateful Streaming API

For complex streaming scenarios, you can combine **lambda streamers** with **compile-time context** to build elegant, composable pipelines. Example:
counting objects in deeply nested GeoJSON without materializing containers:

```cpp
struct Stats {
    std::size_t totalPoints = 0;
    std::size_t totalRings = 0;
    std::size_t totalFeatures = 0;
};

struct Point { double x, y; };
using PointAsArray = A<Point, as_array>;

// Lambda streamers with captured context (Stats*)
using RingConsumer = streamers::LambdaStreamer<[](Stats* stats, const PointAsArray& point) {
    stats->totalPoints++;
    return true;
}>;

using RingsConsumer = streamers::LambdaStreamer<[](Stats* stats, const RingConsumer& ring) {
    stats->totalRings++;
    return true;
}>;

// Nested structure with streaming consumers
struct Feature {
    A<std::string, key<"type">, string_constant<"Feature">> _;
    std::map<std::string, std::string> properties;
    
    struct PolygonGeometry {
        A<std::string, key<"type">, string_constant<"Polygon">> _;
        A<RingsConsumer, key<"coordinates">> rings;
    };
    PolygonGeometry geometry;
};

using FeatureConsumer = streamers::LambdaStreamer<[](Stats* stats, const Feature& feature) {
    stats->totalFeatures++;
    return true;
}>;

struct GeoJSONStatsCounter {
    A<std::string, key<"type">, string_constant<"FeatureCollection">> _;
    FeatureConsumer features;
};

// Usage: count 117K+ coordinates in canada.json without allocating containers
Stats stats;
GeoJSONStatsCounter counter;
auto result = JsonFusion::Parse(counter, json, &stats);
// stats now contains: totalPoints, totalRings, totalFeatures
```

**Key advantages:**
- ✅ **Type-safe context**: `Stats*` captured at compile-time, no `void*` casting
- ✅ **Declarative structure**: Code mirrors JSON schema
- ✅ **Compile-time composition**: Entire pipeline validated before runtime
- ✅ **Zero materialization**: Counts objects without allocating arrays
- ✅ **Still validates**: Enforces `string_constant<"Feature">`, `string_constant<"Polygon">`

This is fundamentally different from traditional SAX:

| Traditional SAX | JsonFusion High-Level Streaming |
|-----------------|--------------------------------|
| Manual state machines | Declarative nested structure |
| `void*` context casting | Type-safe compile-time context |
| No validation | Schema validation while streaming |
| Per-schema custom code | Generic, reusable, composable |

📁 **Real-world example**: [`benchmarks/canada_json/canada_json_parsing.hpp`](benchmarks/canada_json/canada_json_parsing.hpp) (processes 2.2 MB GeoJSON,
117K+ coordinates)

### Compile-Time Testing

Nearly the whole JsonFusion's test suite is `constexpr`-only: JSON/CBOR parsing and serialization run entirely at **compile time** and are verified with `static_assert`. There is no test framework and no runtime harness — the compiler *is* the test runner.

**What this gives you:**

- **Stronger guarantees about core logic**  
  The parts of the library that are marked `constexpr` must be usable in constant evaluation: no hidden global state, no I/O, no thread-local caches, no “only works at runtime” tricks. If any of that sneaks in, the tests simply stop compiling.

- **Early, compiler-enforced validation**  
  Many bugs (wrong return types, broken invariants, incorrect parsing/serialization of edge cases) are caught as compile errors via `static_assert`, before you ever run a binary.

- **Tests the real public API**  
  The tests call the same `Parse` / `Serialize` functions users do. There’s no special “test mode” or backdoor.

- **Good pressure towards “allocation-light” designs**  
  While C++ now allows allocations in `constexpr`, JsonFusion’s core is written so that schema plumbing works with fixed-size storage and trivial types. If core internals started to rely on non–`constexpr` dynamic allocation, the compile-time tests would start failing, which keeps the implementation honest.

- **Very small, very robust test harness**  
  Many tests are just:  
  1. Define a model  
  2. Parse/serialize in `constexpr`  
  3. `static_assert` on the result  

  A simple bash script that compiles all test translation units is enough — no test framework, no runner, no fixture boilerplate.

**Example** (this runs at compile time):
```cpp
struct Config { 
    int value;
    std::string dynamic_string;
    std::vector<std::vector<int>> nested_vecs;
    std::vector<std::optional<std::vector<std::string>>> complex_nested;
};

static_assert([]() constexpr {
    Config c{};
    auto result = JsonFusion::Parse(c, std::string(R"(
        {
            "value": 42,
            "dynamic_string": "allocated at compile time!",
            "nested_vecs": [[1,2], [3,4,5]],
            "complex_nested": [null, ["a","b"], null]
        })"));
    return result 
        && c.value == 42 
        && c.nested_vecs[1][2] == 5
        && c.complex_nested[1]->at(0) == "a";
}()); 
```

This approach aligns with JsonFusion's philosophy: leverage compile-time introspection to prove correctness before the program even runs.

📁 **Test suite details**: [`tests/constexpr/README.md`](tests/constexpr/README.md)

### JSON Schema Generation

JsonFusion models can be automatically expressed as [JSON Schema](https://json-schema.org/draft/2020-12/schema) (Draft 2020-12), including full support for validators, options, and complex nested structures. The schema generation happens at **compile-time** and is fully `constexpr`-compatible.

**Features:**
- ✅ All validators mapped to JSON Schema constraints (`range`, `min_length`, `enum_values`, etc.)
- ✅ All options supported (`key<>`, `as_array`, `indexes_as_keys`, `WireSink`, etc.)
- ✅ Nullable types (`std::optional`, `std::unique_ptr`) generate `oneOf` with `null`
- ✅ Recursive types with self-references to root schema (e.g., trees, linked lists) use `{"$ref": "#"}`
- ✅ Zero runtime overhead - pure compile-time type introspection

**Limitations:**
- ❌ Mutual recursion (A→B→A) and nested recursive types are not supported. Only self-referencing types at the root level generate valid schemas (static assertion on unsupported patterns)

**Full demo**: [`examples/json_schema_demo.cpp`](examples/json_schema_demo.cpp):
```bash
# Compile and run from project root
g++-14 -std=c++23 -I./include -o /tmp/json_schema_demo ./examples/json_schema_demo.cpp && /tmp/json_schema_demo
```

## Limitations and When NOT to Use

- **Requires GCC 14, Clang 20 or newer** (GCC 16+ for the optional C++26 reflection mode). Other compilers (MSVC, older GCC versions) are not currently supported due to template instantiation complexity and performance characteristics.
- Designed for C++23 aggregates (POD-like structs). Classes with custom constructors, virtual methods, etc. are not automatically reflectable. *(C++26 reflection removes this limitation—non-aggregate types are fully supported with GCC 16+ and `-std=c++26 -freflection`.)*
- Relies on PFR in C++20/23 mode; a few exotic compilers/ABIs may not be supported. *(C++26 reflection mode has zero external dependencies.)*
- **`THIS IS NOT A JSON DOM LIBRARY.`** It shines when you have a known schema and want to map JSON directly from/into C++ types; if you need a generic JSON tree and ad-hoc editing, JsonFusion is not the right tool; consider using it alongside a DOM library.
- **Floating-point numbers handling**: JsonFusion uses an in-house constexpr-compatible FP parser/serializer by default (`JSONFUSION_FP_BACKEND=0`). While tested for correctness and sufficient for typical use cases, it is not as precise or fast as state-of-the-art implementations like fast_float. For applications with extreme FP requirements, the backend is swappable—see [docs/FP_BACKEND_ARCHITECTURE.md](docs/FP_BACKEND_ARCHITECTURE.md) for details on alternative backends or implementing custom FP handling. Or just use `YyjsonReader` to get both speed boost and FP correctness.