## JsonFusion Types Annotation Reference

### Annotation Syntax

#### Standard Syntax (C++20/23)

```cpp
Annotated<T, Validator1, Validator2, Option1, ...> field;
A<T, Validator1, Validator2, Option1, ...> field; // shorthand alias
```

#### C++26 Native Annotation Syntax

With C++26 reflection (`-std=c++26 -freflection` in GCC 16+), you can use native attributes instead of wrapper types:

```cpp
[[=A<Validator1, Validator2, Option1, ...>{}]] T field;
```
**Example comparison:**
```cpp
// C++20/23 (wrapper syntax)
struct Config {
    Annotated<int, range<0, 65535>> port;
    A<int, range<1, 100>> max_connections;
};

// C++26 (native annotation syntax)
struct Config {
    [[=A<range<0, 65535>>{}]] int port;
    [[=A<range<1, 100>>{}]] int max_connections;
};
```

**Note:** Both syntaxes can be mixed in the same struct. The `A<...>` wrapper is required to distinguish JsonFusion annotations from other libraries' attributes.

### Validators (Type-Specific Constraints)

#### Number Validators
```cpp
range<Min, Max>              // Value must be in [Min, Max]
constant<Value>              // Value must equal Value (works for bool, numbers)
```

#### String Validators
```cpp
min_length<N>                // String must have at least N characters
max_length<N>                // String must have at most N characters (streaming)
enum_values<"val1", ...>     // String must be one of the listed values (streaming)
string_constant<"value">     // String must equal exactly "value"
```

#### Array Validators
```cpp
min_items<N>                 // Array must have at least N elements
max_items<N>                 // Array must have at most N elements (streaming)
```

#### Struct Validators
```cpp
not_required<"field1", ...>  // Mark specific fields as optional - allows field to be absent from JSON (struct-level)
required<"field1", ...>  // Mark specific fields as required - forces field presence in JSON (struct-level)
forbidden<"field1", ...> // Mark specific fields as forbidden: error on presense
allow_excess_fields  // Allow unknown JSON fields (don't reject and silently skip)
```

**Important**: `std::optional<T>`/`std::unique_ptr<T>` allows `null` values but the field presence is controlled by `not_required`/`required`

#### Map Validators
```cpp
// Entry count
min_properties<N>            // Map must have at least N entries
max_properties<N>            // Map must have at most N entries (streaming)

// Key constraints
min_key_length<N>            // All keys must have at least N characters
max_key_length<N>            // All keys must have at most N characters (streaming)

// Key whitelist/blacklist
required_keys<"k1", ...>     // These keys MUST be present
allowed_keys<"k1", ...>      // ONLY these keys are allowed (streaming)
forbidden_keys<"k1", ...>    // These keys are FORBIDDEN (streaming)
```

#### Generic Custom Validators

JsonFusion's validation system is event-driven. You can attach custom validation logic to parsing events using `fn_validator<Event, Callable>`.

**Syntax:**
```cpp
fn_validator<EventTag, Callable>
```

where:
- `EventTag` — A parsing event tag from `JsonFusion::validators_detail::parsing_events_tags`
- `Callable` — A stateless lambda or free function with signature matching the event

**Available Events & Signatures:**

The validator function can use simplified signatures (without `ValidationCtx`). The framework supports multiple signature variants but these are the most user-friendly:

| Event Tag | Triggers When | User Signature (simplified) | Example |
|-----------|--------------|------------------------------|---------|
| `bool_parsing_finished` | After parsing bool | `bool fn(const bool& value)` | `[](bool v) { return v == true; }` |
| `number_parsing_finished` | After parsing number | `bool fn(const T& value)` | `[](int v) { return v % 10 == 0; }` |
| `string_parsing_finished` | After parsing string | `bool fn(const Storage&, const std::string_view& value)` | `[](const auto&, const auto& s) { return s.size() > 5; }` |
| `array_item_parsed` | After each array item | `bool fn(const Container&, std::size_t count)` | `[](const auto&, size_t n) { return n <= 100; }` |
| `array_parsing_finished` | After complete array | `bool fn(const Container&, std::size_t count)` | `[](const auto& arr, size_t n) { return n >= 1; }` |
| `object_parsing_finished` | After complete struct | `bool fn(const Struct&, const auto& seen, const auto&)` | `[](const auto& obj, auto, auto) { return obj.isValid(); }` |
| `destructured_object_parsing_finished` | After `as_array` struct | `bool fn(const Struct&)` | `[](const Point& p) { return p.x >= 0; }` |
| `map_key_finished` | After each map key | `bool fn(const Map&, const std::string_view& key)` | `[](const auto&, const auto& k) { return k.size() <= 50; }` |
| `map_value_parsed` | After each map value | `bool fn(const Map&, std::size_t count)` | `[](const auto&, size_t n) { return n <= 1000; }` |
| `map_entry_parsed` | After key+value pair | `bool fn(const Map&, std::size_t count)` | `[](const auto&, size_t n) { return n <= 1000; }` |
| `map_parsing_finished` | After complete map | `bool fn(const Map&, std::size_t count)` | `[](const auto&, size_t n) { return n >= 1; }` |

**Note**: The first parameter is always the storage object being validated. For many validators, you can ignore it using `const auto&` and focus on the phase-specific parameters (like `std::string_view` for strings, `std::size_t count` for arrays/maps).


### Options (Metadata & Behavior Control)

#### Field-Level Options
```cpp
key<"field_name">           // Override JSON key name (use "field_name" instead of C++ field name)
int_key<N>                  // Use integer N as key (for indexes_as_keys mode). Native int indexes in CBOR
exclude                     // Exclude field from JSON serialization/deserialization
skip                        // Fast-skip this value during parsing/serialization
```

#### Struct-Level Options
```cpp
as_array                    // Serialize/parse struct as JSON array instead of object: [x, y, z] <-> struct{float x, y, z;}
indexes_as_keys             // Use field indices as JSON keys: {"0": val0, "1": val1, ...} <-> struct. Native int indexes in CBOR
skip_nulls                  // Skip null values during serialization (for null nullable fields, works in map-like structures to allow sparse index space)
```

#### Composition Options

```cpp
flatten                     // Promote a composed field's members to the parent JSON level (C++26 only)
```

Annotate a struct-typed field with `flatten` to "unwrap" it — its members appear at the parent level in JSON instead of as a nested object. Works with both annotation and wrapper syntax:

```cpp
struct Address {
    std::string city;
    std::string zip;
};

// C++26 annotation syntax
struct Person {
    std::string name;
    [[=A<flatten>{}]] Address address;
};

// Wrapper syntax
struct Person {
    std::string name;
    A<Address, flatten> address;
};
```

Both produce/consume flat JSON:
```json
{"name": "Alice", "city": "NYC", "zip": "10001"}
```

**Features:**
- Multiple `flatten` fields in the same struct are supported
- Inner fields retain their own validators (e.g. `range<>` on an inner field still works)
- The flattened type's own base classes are included (inheritance + flatten compose naturally)
- `flatten` can coexist with inheritance flattening in the same struct

**Inheritance flattening** is automatic and requires no annotation — base class fields always appear at the derived class's JSON level:

```cpp
struct Base {
    int id;
    std::string name;
};

struct Derived : Base {
    std::string email;
};
// JSON: {"id": 1, "name": "Alice", "email": "alice@example.com"}
```

Multiple inheritance works the same way (mixin pattern). If a derived class declares a field with the same name as a base class field, the derived field wins.

**NOTE:** Both `flatten` and inheritance flattening are C++26-only features (require `-std=c++26 -freflection`). They rely on `std::meta::bases_of` and annotation reflection which are not available in the Boost.PFR fallback path.



