# How-to Guides

Task-oriented recipes for common JsonFusion scenarios.
Each guide solves one specific problem with minimal, copy-paste-ready code.

---

## Parsing & Serialization

| Guide | Summary |
|-------|---------|
| [Parse JSON into a struct](parse-json-into-struct.md) | Basic `Parse()` call, nested structs, error checking |
| [Serialize a struct to JSON](serialize-struct-to-json.md) | Basic `Serialize()` call, output to `std::string`, pretty-printing |
| [Round-trip JSON (parse then serialize)](round-trip-json.md) | Parse input, modify in C++, serialize back |
| [Use optional fields](optional-fields.md) | `std::optional<T>` and `std::unique_ptr<T>` for nullable JSON values |
| [Work with arrays and vectors](arrays-and-vectors.md) | `std::vector<T>`, `std::array<T,N>`, `std::list<T>`, nested arrays |
| [Work with maps](maps.md) | `std::map<std::string, T>` for JSON objects with dynamic keys |
| [Handle unknown/extra fields](handle-extra-fields.md) | `allow_excess_fields` option to silently ignore unexpected keys |
| [Default values and default missing fields behaviour](default-values.md) | Objects parsing lifecycle and default behaviour, working with default values |
| [Integrating with std streams for IO](std-streams-json-io.md) | Working with std::istream and similar IO streams |

## Validation & Constraints

| Guide | Summary |
|-------|---------|
| [Validate number ranges](validate-number-ranges.md) | `range<Min, Max>` constraint on int/float fields |
| [Validate string length](validate-string-length.md) | `min_length<N>`, `max_length<N>` constraints |
| [Restrict to enum values](restrict-enum-values.md) | `enum_values<...>` to accept only specific values |
| [Enforce constant values](enforce-constant-values.md) | `constant<V>` and `string_constant<"...">` for fixed fields |
| [Validate array/map sizes](validate-collection-sizes.md) | `min_items<N>`, `max_items<N>` for collections |
| [Write a custom validator](custom-validator.md) | `fn_validator<Event, Lambda>` for arbitrary validation logic |
| [Cross-field constraints](cross-field-constraints.md) | Validate object, where fields correctness depend on each other |

## Error handling

| Guide | Summary |
|-------|---------|
| [Working with parse/serialize result objects](result-objects-basics.md) | Printing detailed errors info |
| [Advanced custom error handling](advanced-error-handling.md) | Extracting detailed info from result objects, handling parsing/validation errors, working with JSON-path |

## Annotations & Field Options

| Guide | Summary |
|-------|---------|
| [Rename a JSON key](rename-json-key.md) | `key<"wire_name">` to decouple C++ names from JSON keys |
| [Exclude a field from JSON](exclude-field.md) | `exclude` to skip fields during parse/serialize |
| [Skip parsing a field's value](skip-field-value.md) | `skip` to validate structure but discard the value (performance) |
| [Serialize a struct as a JSON array](struct-as-array.md) | `as_array` to emit `[x, y, z]` instead of `{"x":...,"y":...}` |
| [Annotate types externally](external-annotations.md) | `Annotated<T>`, `AnnotatedField<T, Index>`, `StructMeta<T>` for third-party or pure types |

## How to integrate with...

| Guide | Summary |
|-------|---------|
| [PlatformIO](../PLATFORMIO.md) | Use JsonFusion in `PlatformIO` projects |
| [vcpkg](../vcpkg_usage.md) | Use JsonFusion with `vcpkg` package manager |
| [conan](../conan_usage.md) | Use JsonFusion with `conan` package manager |

## CBOR (Binary Format)

| Guide | Summary |
|-------|---------|
| [Parse and serialize CBOR](parse-serialize-cbor.md) | `CborReader` / `CborWriter` with the same type system |
| [Convert between JSON and CBOR](convert-json-cbor.md) | Parse from one format, serialize to the other |
| [Size-efficient CBOR](size-optimized-cbor.md) | Use CBOR integer keys in mappings to minimize wire data and parser/serializer code size |

## YAML

| Guide | Summary |
|-------|---------|
| [Parse and serialize YAML](parse-serialize-yaml.md) | `RapidYamlReader` / `RapidYamlWriter` with the same type system |

## Custom Types & Transformers

| Guide | Summary |
|-------|---------|
| [Working with strongly typed enums](cpp-enums.md) | Creating custom transformers for `enum`s and `enum class`es handling,  `magic_enum` integration demo |
| [Transform a wire type into a domain type](transform-wire-to-domain.md) | Creating custom transformers for dates, IDs, etc. |
| [Handle schema evolution](schema-evolution.md) | Accept old and new wire formats with `WireSink` + multi-attempt parsing |
| [Create and use variant-like types](variant-types.md) | `VariantOneOf<Ts...>` to try multiple alternatives |

## Composing and reusing models

| Guide | Summary |
|-------|---------|
| [Create reusable templated submodels](composable-reusable-types.md) | DRY principles in JsonFusion, approaches for improving maintainability, composing complex models |
| [Struct composition with flatten and inheritance](struct-composition.md) | Flatten composed fields into parent JSON level, inheritance flattening, mixin pattern — C++26 only |

## Streaming (Typed SAX)

| Guide | Summary |
|-------|---------|
| [Stream-serialize without containers](streaming-producer.md) | Producing streamer: generate array elements on demand |
| [Stream-parse without containers](streaming-consumer.md) | Consuming streamer: process elements one by one, zero allocation |
| [Use lambda streamers with context](lambda-streamers.md) | `LambdaStreamer<Lambda>` with typed context pointer |
| [Compose nested streamers](nested-streamers.md) | Streamers containing streamers for jagged arrays or deep structures |
| [Unlimited streams handling](unlimited-map-reducing.md) | Handle documents of arbitrary sizes using streaming with on-the-fly map-reducing and O(1) memory consumption |

## Embedded & Fixed-Size

| Guide | Summary |
|-------|---------|
| [Use fixed-size containers only](fixed-size-containers.md) | `std::array<char, N>` strings, `std::array<T, N>` arrays — no heap |
| [Byte-by-byte IO](iterators-io.md) | Iterator API: `Serialize(obj, begin, end)`/`Parse(obj, begin, end)` |
| [Estimate maximum serialized size](size-estimation.md) | `EstimateMaxSerializedSize<T>()` for fixed-buffer allocation |

## Working with untyped data
| Guide | Summary |
|-------|---------|
| [Capture raw JSON with WireSink](wiresink.md) | Passthrough unparsed fragments between formats |

## Achieving maximum throughput
| Guide | Summary |
|-------|---------|
| [Use the yyjson backend](yyjson-backend.md) | Drop-in `YyjsonReader` / `YyjsonWriter` for DOM-based speed |

## JSON schema and schema algebra

| Guide | Summary |
|-------|---------|
| [Generate JSON Schema from types](json-schema-generation.md) | Automatic Draft 2020-12 schema with all validators mapped |
| [Use WireSink with yyjson for O(1) capture](yyjson-wiresink.md) | DOM-pointer capture instead of text re-serialization |

## Constexpr & Compile-Time

| Guide | Summary |
|-------|---------|
| [Parse JSON/CBOR at compile time](constexpr-parse.md) | `constexpr` Parse with `static_assert` validation |
| [Run tests at compile time](constexpr-tests.md) | Use `static_assert` as a test framework — no runtime needed |

## C Interoperability

| Guide | Summary |
|-------|---------|
| [Parse JSON into C structs](c-interop-parse.md) | `StructMeta<T>` for C arrays and plain-C types |
| [Expose a C API for JSON parsing](c-interop-api.md) | `extern "C"` wrapper functions callable from pure C code |

## Migrating from other libs

| Guide | Summary |
|-------|---------|
| [nlohmann/json, RapidJSON, ArduinoJSON](migrating-from-dom-based-libs.md) | How to start using JsonFusion instead of DOM-based libs and how to restructure the code to take full advantage of "validation-as-a-boundary" principle |
| [Glaze, reflect-cpp](migrating-from-reflection-based-libs.md) | How to start using JsonFusion instead of other reflection-based type-driven libs |

## Implementing custom protocols

| Guide | Summary |
|-------|---------|
| [Use the pluggable reader/writer interface](custom-reader-writer.md) | Implement `ReaderLike` / `WriterLike` for a new format |

## Getting started with C++26 annotations and introspection

| Guide | Summary |
|-------|---------|
| [Use C++26 annotations to attach metadata](cpp26-annotations.md) | Unleash c++26 to work with arbitrary non-aggregate structs and classes and attach metadata to fields without polluting your types system with JsonFusion A<> wrappers  |
| [Use C++26 introspection to create automatic enums transformers](cpp26-enum-introspecting-transformers.md) | Convert enum identifiers to text strings and back with c++26 introspection to create automatic enum transformers |

## Implementing custom floating-point backend

| Guide | Summary |
|-------|---------|
| [Create JSON FP backend with custom properties](custom-json-fp-backends.md) | Using JsonFusion JSON FP backend interface to implement custom floating-point parsing/serializing |


---

[Back to Documentation](../index.md)
