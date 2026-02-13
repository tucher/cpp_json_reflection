// ============================================================================
// C++26 Struct Composition Test
// ============================================================================
// Compile with: ~/gcc_latest/install/bin/g++ -std=c++26 -freflection \
//               -I./include -I./tests/constexpr \
//               -fsyntax-only tests/constexpr/composition/test_compopsition_basic.cpp
//
// This test verifies inheritance flattening, mixin support, and explicit
// composition flattening (options::flatten) with both C++26 annotation
// and wrapper type syntaxes.
//
// NOTE: This test is skipped (compiles to empty) when C++26 reflection is not available.
// ============================================================================

#include <JsonFusion/struct_introspection.hpp>

#if JSONFUSION_USE_REFLECTION

#include <JsonFusion/parser.hpp>
#include <JsonFusion/validators.hpp>

using namespace JsonFusion;
using namespace JsonFusion::validators;

// ============================================================================
// Inheritance flattening — base class fields in flat JSON
// ============================================================================

// --- Simple single inheritance ---

struct InheritBase {
    int id;
    std::string name;
};

struct InheritDerived : InheritBase {
    std::string email;
};

constexpr bool test_simple_inheritance() {
    InheritDerived obj{};
    std::string json = R"({"id": 42, "name": "Alice", "email": "alice@test.com"})";
    auto result = Parse(obj, json);
    return result && obj.id == 42 && obj.name == "Alice" && obj.email == "alice@test.com";
}
static_assert(test_simple_inheritance(),
    "Simple inheritance: base fields are flattened into JSON");

// --- Multi-level inheritance: A -> B -> C ---

struct LevelA {
    int a_field;
};

struct LevelB : LevelA {
    int b_field;
};

struct LevelC : LevelB {
    int c_field;
};

constexpr bool test_multi_level_inheritance() {
    LevelC obj{};
    std::string json = R"({"a_field": 1, "b_field": 2, "c_field": 3})";
    auto result = Parse(obj, json);
    return result && obj.a_field == 1 && obj.b_field == 2 && obj.c_field == 3;
}
static_assert(test_multi_level_inheritance(),
    "Multi-level inheritance: all ancestor fields flattened");

// --- Multiple inheritance (mixin pattern) ---

struct MixinTimestamps {
    std::string created_at;
    std::string updated_at;
};

struct MixinIdentifiable {
    int id;
    std::string name;
};

struct MixinUser : MixinIdentifiable, MixinTimestamps {
    std::string email;
};

constexpr bool test_multiple_inheritance_mixin() {
    MixinUser obj{};
    std::string json = R"({"id": 1, "name": "Alice", "created_at": "2024-01-01", "updated_at": "2024-06-01", "email": "alice@test.com"})";
    auto result = Parse(obj, json);
    return result
        && obj.id == 1 && obj.name == "Alice"
        && obj.created_at == "2024-01-01" && obj.updated_at == "2024-06-01"
        && obj.email == "alice@test.com";
}
static_assert(test_multiple_inheritance_mixin(),
    "Multiple inheritance (mixin): fields from all bases flattened");

// --- Inheritance with C++26 annotations on base class fields ---

struct AnnotatedBase {
    [[=A<range<0, 65535>>{}]] int port;
};

struct AnnotatedDerived : AnnotatedBase {
    std::string host;
};

constexpr bool test_inheritance_annotations_valid() {
    AnnotatedDerived obj{};
    std::string json = R"({"port": 8080, "host": "localhost"})";
    auto result = Parse(obj, json);
    return result && obj.port == 8080 && obj.host == "localhost";
}
static_assert(test_inheritance_annotations_valid(),
    "Inheritance with annotations: base annotations preserved and valid");

constexpr bool test_inheritance_annotations_invalid() {
    AnnotatedDerived obj{};
    std::string json = R"({"port": 99999, "host": "localhost"})";
    auto result = Parse(obj, json);
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_inheritance_annotations_invalid(),
    "Inheritance with annotations: base validation still enforced");

// --- Inheritance with annotations on both base and derived ---

struct AnnotatedBase2 {
    [[=A<range<0, 100>>{}]] int base_val;
};

struct AnnotatedDerived2 : AnnotatedBase2 {
    [[=A<range<-10, 10>>{}]] int derived_val;
};

constexpr bool test_inheritance_annotations_both_valid() {
    AnnotatedDerived2 obj{};
    std::string json = R"({"base_val": 50, "derived_val": 5})";
    auto result = Parse(obj, json);
    return result && obj.base_val == 50 && obj.derived_val == 5;
}
static_assert(test_inheritance_annotations_both_valid(),
    "Annotations on both base and derived: all valid");

constexpr bool test_inheritance_annotations_base_fails() {
    AnnotatedDerived2 obj{};
    std::string json = R"({"base_val": 200, "derived_val": 5})";
    auto result = Parse(obj, json);
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_inheritance_annotations_base_fails(),
    "Annotations on both base and derived: base validation fails");

constexpr bool test_inheritance_annotations_derived_fails() {
    AnnotatedDerived2 obj{};
    std::string json = R"({"base_val": 50, "derived_val": 99})";
    auto result = Parse(obj, json);
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_inheritance_annotations_derived_fails(),
    "Annotations on both base and derived: derived validation fails");

// --- Multi-level with annotations at each level ---

struct AnnLevelA {
    [[=A<range<0, 10>>{}]] int a;
};

struct AnnLevelB : AnnLevelA {
    [[=A<range<0, 100>>{}]] int b;
};

struct AnnLevelC : AnnLevelB {
    [[=A<range<0, 1000>>{}]] int c;
};

constexpr bool test_multi_level_annotations_valid() {
    AnnLevelC obj{};
    std::string json = R"({"a": 5, "b": 50, "c": 500})";
    auto result = Parse(obj, json);
    return result && obj.a == 5 && obj.b == 50 && obj.c == 500;
}
static_assert(test_multi_level_annotations_valid(),
    "Multi-level inheritance with annotations at each level: all valid");

constexpr bool test_multi_level_annotations_grandparent_fails() {
    AnnLevelC obj{};
    std::string json = R"({"a": 99, "b": 50, "c": 500})";
    auto result = Parse(obj, json);
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_multi_level_annotations_grandparent_fails(),
    "Multi-level inheritance: grandparent validation enforced");

// --- Partial JSON with inheritance (unset fields keep defaults) ---

struct DefaultBase {
    int x = 10;
    int y = 20;
    
    constexpr DefaultBase() = default;
};

struct DefaultDerived : DefaultBase {
    int z = 30;
    
    constexpr DefaultDerived() = default;
};

constexpr bool test_inheritance_partial_json() {
    DefaultDerived obj{};
    std::string json = R"({"x": 99})";
    auto result = Parse(obj, json);
    return result && obj.x == 99 && obj.y == 20 && obj.z == 30;
}
static_assert(test_inheritance_partial_json(),
    "Inheritance with partial JSON: unset fields keep defaults");

// --- Field override: derived shadows base field with same name ---

struct OverrideBase {
    int value;
    std::string label;
};

struct OverrideDerived : OverrideBase {
    int value;  // shadows OverrideBase::value
};

constexpr bool test_field_override() {
    OverrideDerived obj{};
    std::string json = R"({"value": 42, "label": "test"})";
    auto result = Parse(obj, json);
    // Derived 'value' wins; base 'label' is still accessible
    return result && obj.OverrideDerived::value == 42 && obj.label == "test";
}
static_assert(test_field_override(),
    "Field override: derived field shadows base field with same name");

// ============================================================================
// Explicit composition flattening — C++26 annotation syntax
// ============================================================================

// --- Basic flatten: composed struct fields at parent level ---

struct FlatAddress {
    std::string city;
    std::string zip;
};

struct FlatPerson {
    std::string name;
    [[=A<options::flatten>{}]] FlatAddress address;
};

constexpr bool test_flatten_basic() {
    FlatPerson obj{};
    std::string json = R"({"name": "Alice", "city": "NYC", "zip": "10001"})";
    auto result = Parse(obj, json);
    return result && obj.name == "Alice"
        && obj.address.city == "NYC" && obj.address.zip == "10001";
}
static_assert(test_flatten_basic(),
    "Flatten: composed struct fields appear at parent JSON level");

// --- Multiple flattened fields ---

struct FlatGeo {
    double lat;
    double lon;
};

struct FlatContact {
    std::string email;
    std::string phone;
};

struct FlatRecord {
    int id;
    [[=A<options::flatten>{}]] FlatContact contact;
    [[=A<options::flatten>{}]] FlatGeo location;
};

constexpr bool test_flatten_multiple() {
    FlatRecord obj{};
    std::string json = R"({"id": 1, "email": "a@b.com", "phone": "555", "lat": 40.7, "lon": -74.0})";
    auto result = Parse(obj, json);
    return result && obj.id == 1
        && obj.contact.email == "a@b.com" && obj.contact.phone == "555"
        && obj.location.lat == 40.7 && obj.location.lon == -74.0;
}
static_assert(test_flatten_multiple(),
    "Flatten: multiple flattened fields in same struct");

// --- Flatten with annotations on inner fields ---

struct FlatValidated {
    [[=A<range<0, 65535>>{}]] int port;
    [[=A<range<1, 100>>{}]] int max_conn;
};

struct FlatServer {
    std::string host;
    [[=A<options::flatten>{}]] FlatValidated config;
};

constexpr bool test_flatten_with_inner_annotations_valid() {
    FlatServer obj{};
    std::string json = R"({"host": "localhost", "port": 8080, "max_conn": 50})";
    auto result = Parse(obj, json);
    return result && obj.host == "localhost"
        && obj.config.port == 8080 && obj.config.max_conn == 50;
}
static_assert(test_flatten_with_inner_annotations_valid(),
    "Flatten: inner field annotations are preserved and valid");

constexpr bool test_flatten_with_inner_annotations_invalid() {
    FlatServer obj{};
    std::string json = R"({"host": "localhost", "port": 99999, "max_conn": 50})";
    auto result = Parse(obj, json);
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_flatten_with_inner_annotations_invalid(),
    "Flatten: inner field annotations enforce validation");

// --- Flatten combined with inheritance ---

struct FlatDerivedServer : FlatServer {
    int priority;
};

constexpr bool test_flatten_with_inheritance() {
    FlatDerivedServer obj{};
    std::string json = R"({"host": "localhost", "port": 443, "max_conn": 10, "priority": 5})";
    auto result = Parse(obj, json);
    return result && obj.host == "localhost"
        && obj.config.port == 443 && obj.config.max_conn == 10
        && obj.priority == 5;
}
static_assert(test_flatten_with_inheritance(),
    "Flatten combined with inheritance: both work together");

// --- Flatten of a struct that itself has base classes ---

struct FlatBaseConfig {
    int timeout;
};

struct FlatExtendedConfig : FlatBaseConfig {
    int retries;
};

struct FlatApp {
    std::string name;
    [[=A<options::flatten>{}]] FlatExtendedConfig config;
};

constexpr bool test_flatten_inner_with_bases() {
    FlatApp obj{};
    std::string json = R"({"name": "myapp", "timeout": 30, "retries": 3})";
    auto result = Parse(obj, json);
    return result && obj.name == "myapp"
        && obj.config.timeout == 30 && obj.config.retries == 3;
}
static_assert(test_flatten_inner_with_bases(),
    "Flatten: inner type's base class fields are also flattened");

// --- Flatten with partial JSON (unset fields keep defaults) ---

struct FlatDefaults {
    int x = 10;
    int y = 20;
    
    constexpr FlatDefaults() = default;
};

struct FlatOuter {
    int z = 30;
    [[=A<options::flatten>{}]] FlatDefaults inner;
    
    constexpr FlatOuter() = default;
};

constexpr bool test_flatten_partial_json() {
    FlatOuter obj{};
    std::string json = R"({"x": 99})";
    auto result = Parse(obj, json);
    return result && obj.inner.x == 99 && obj.inner.y == 20 && obj.z == 30;
}
static_assert(test_flatten_partial_json(),
    "Flatten with partial JSON: unset fields keep defaults");

// ============================================================================
// Explicit composition flattening — wrapper type A<T, flatten> syntax
// ============================================================================

// --- Basic wrapper flatten ---

struct WrapAddress {
    std::string city;
    std::string zip;
};

struct WrapPerson {
    std::string name;
    A<WrapAddress, options::flatten> address;  // wrapper syntax
};

constexpr bool test_flatten_wrapper_basic() {
    WrapPerson obj{};
    std::string json = R"({"name": "Bob", "city": "LA", "zip": "90001"})";
    auto result = Parse(obj, json);
    return result && obj.name == "Bob"
        && obj.address.get().city == "LA" && obj.address.get().zip == "90001";
}
static_assert(test_flatten_wrapper_basic(),
    "Flatten wrapper: A<T, flatten> fields appear at parent JSON level");

// --- Wrapper flatten with inner annotations ---

struct WrapValidated {
    [[=A<range<1, 65535>>{}]] int port;
    std::string host;
};

struct WrapServer {
    int priority;
    A<WrapValidated, options::flatten> config;
};

constexpr bool test_flatten_wrapper_with_inner_annotations_valid() {
    WrapServer obj{};
    std::string json = R"({"priority": 1, "port": 8080, "host": "localhost"})";
    auto result = Parse(obj, json);
    return result && obj.priority == 1
        && obj.config.get().port == 8080 && obj.config.get().host == "localhost";
}
static_assert(test_flatten_wrapper_with_inner_annotations_valid(),
    "Flatten wrapper: inner field annotations preserved and valid");

constexpr bool test_flatten_wrapper_with_inner_annotations_invalid() {
    WrapServer obj{};
    std::string json = R"({"priority": 1, "port": 99999, "host": "localhost"})";
    auto result = Parse(obj, json);
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_flatten_wrapper_with_inner_annotations_invalid(),
    "Flatten wrapper: inner field annotations enforce validation");

// --- Multiple wrapper flattened fields ---

struct WrapGeo {
    double lat;
    double lon;
};

struct WrapContact {
    std::string email;
    std::string phone;
};

struct WrapRecord {
    int id;
    A<WrapContact, options::flatten> contact;
    A<WrapGeo, options::flatten> location;
};

constexpr bool test_flatten_wrapper_multiple() {
    WrapRecord obj{};
    std::string json = R"({"id": 7, "email": "x@y.com", "phone": "123", "lat": 51.5, "lon": -0.1})";
    auto result = Parse(obj, json);
    return result && obj.id == 7
        && obj.contact.get().email == "x@y.com" && obj.contact.get().phone == "123"
        && obj.location.get().lat == 51.5 && obj.location.get().lon == -0.1;
}
static_assert(test_flatten_wrapper_multiple(),
    "Flatten wrapper: multiple A<T, flatten> fields in same struct");

// --- Mix of both syntaxes in one struct ---

struct MixFlatInner1 {
    int a;
};

struct MixFlatInner2 {
    int b;
};

struct MixFlatOuter {
    int own;
    [[=A<options::flatten>{}]] MixFlatInner1 annotation_style;
    A<MixFlatInner2, options::flatten> wrapper_style;
};

constexpr bool test_flatten_mixed_syntaxes() {
    MixFlatOuter obj{};
    std::string json = R"({"own": 1, "a": 2, "b": 3})";
    auto result = Parse(obj, json);
    return result && obj.own == 1
        && obj.annotation_style.a == 2
        && obj.wrapper_style.get().b == 3;
}
static_assert(test_flatten_mixed_syntaxes(),
    "Flatten: both [[=A<flatten>{}]] and A<T, flatten> work in same struct");

#endif // JSONFUSION_USE_REFLECTION
