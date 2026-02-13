// ============================================================================
// C++26 Annotations Basic Test
// ============================================================================
// Compile with: ~/gcc_latest/install/bin/g++ -std=c++26 -freflection \
//               -I/Users/tucher/JsonFusion/include \
//               tests/constexpr/annotations/test_cpp26_annotations_basic.cpp
//
// This test verifies that C++26 annotations (via [[=A<...>{}]])
// are correctly extracted and used for validation during JSON parsing.
//
// NOTE: This test is skipped (compiles to empty) when C++26 reflection is not available.
// ============================================================================

// Check for C++26 reflection support
#include <JsonFusion/struct_introspection.hpp>

#if JSONFUSION_USE_REFLECTION
// ============================================================================
// C++26 REFLECTION AVAILABLE - Run all tests
// ============================================================================

#include <JsonFusion/parser.hpp>
#include <JsonFusion/validators.hpp>

using namespace JsonFusion;
using namespace JsonFusion::validators;

// ============================================================================
// Test: Basic range validation with C++26 annotations
// ============================================================================

// Instead of: Annotated<int, range<0, 100>> value;
// We use:     [[=A<range<0, 100>>{}]] int value;

// Test: range<0, 100> accepts value at min boundary
constexpr bool test_annotation_range_min_boundary_valid() {
    struct Test {
        [[=A<range<0, 100>>{}]] int value;
    };
    
    Test obj{};
    std::string json = R"({"value": 0})";
    auto result = Parse(obj, json);
    
    return result && obj.value == 0;
}
static_assert(test_annotation_range_min_boundary_valid(), 
    "[[=A<range<0, 100>>{}]] accepts min boundary (0)");

// Test: range<0, 100> accepts value at max boundary
constexpr bool test_annotation_range_max_boundary_valid() {
    struct Test {
        [[=A<range<0, 100>>{}]] int value;
    };
    
    Test obj{};
    std::string json = R"({"value": 100})";
    auto result = Parse(obj, json);
    
    return result && obj.value == 100;
}
static_assert(test_annotation_range_max_boundary_valid(), 
    "[[=A<range<0, 100>>{}]] accepts max boundary (100)");

// Test: range<0, 100> accepts value in middle
constexpr bool test_annotation_range_middle_valid() {
    struct Test {
        [[=A<range<0, 100>>{}]] int value;
    };
    
    Test obj{};
    std::string json = R"({"value": 50})";
    auto result = Parse(obj, json);
    
    return result && obj.value == 50;
}
static_assert(test_annotation_range_middle_valid(), 
    "[[=A<range<0, 100>>{}]] accepts middle value (50)");

// ============================================================================
// Test: range<> - Invalid Values (Below Min, Above Max)
// ============================================================================

// Test: range<0, 100> rejects value below min
constexpr bool test_annotation_range_below_min() {
    struct Test {
        [[=A<range<0, 100>>{}]] int value;
    };
    
    Test obj{};
    std::string json = R"({"value": -1})";
    auto result = Parse(obj, json);
    
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_annotation_range_below_min(), 
    "[[=A<range<0, 100>>{}]] rejects value below min (-1)");

// Test: range<0, 100> rejects value above max
constexpr bool test_annotation_range_above_max() {
    struct Test {
        [[=A<range<0, 100>>{}]] int value;
    };
    
    Test obj{};
    std::string json = R"({"value": 101})";
    auto result = Parse(obj, json);
    
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_annotation_range_above_max(), 
    "[[=A<range<0, 100>>{}]] rejects value above max (101)");

// ============================================================================
// Test: Multiple annotated fields
// ============================================================================

constexpr bool test_annotation_multiple_fields() {
    struct Config {
        [[=A<range<0, 65535>>{}]] int port;
        [[=A<range<1, 100>>{}]] int max_connections;
        int plain_field;  // No annotation - no validation
    };
    
    Config obj{};
    std::string json = R"({"port": 8080, "max_connections": 50, "plain_field": 999999})";
    auto result = Parse(obj, json);
    
    return result 
        && obj.port == 8080 
        && obj.max_connections == 50
        && obj.plain_field == 999999;
}
static_assert(test_annotation_multiple_fields(), 
    "Multiple annotated fields with range validation");

constexpr bool test_annotation_multiple_fields_one_fails() {
    struct Config {
        [[=A<range<0, 65535>>{}]] int port;
        [[=A<range<1, 100>>{}]] int max_connections;
    };
    
    Config obj{};
    std::string json = R"({"port": 8080, "max_connections": 150})";
    auto result = Parse(obj, json);
    
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_annotation_multiple_fields_one_fails(), 
    "Multiple annotated fields - one fails validation");

// ============================================================================
// Test: Negative range values
// ============================================================================

constexpr bool test_annotation_negative_range() {
    struct Test {
        [[=A<range<-100, -10>>{}]] int value;
    };
    
    Test obj{};
    std::string json = R"({"value": -50})";
    auto result = Parse(obj, json);
    
    return result && obj.value == -50;
}
static_assert(test_annotation_negative_range(), 
    "[[=A<range<-100, -10>>{}]] accepts value in negative range");

constexpr bool test_annotation_negative_range_rejects_positive() {
    struct Test {
        [[=A<range<-100, -10>>{}]] int value;
    };
    
    Test obj{};
    std::string json = R"({"value": 5})";
    auto result = Parse(obj, json);
    
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_annotation_negative_range_rejects_positive(), 
    "[[=A<range<-100, -10>>{}]] rejects positive value");

// ============================================================================
// Test: Single-value range
// ============================================================================

constexpr bool test_annotation_single_value_range() {
    struct Test {
        [[=A<range<42, 42>>{}]] int value;
    };
    
    Test obj{};
    std::string json = R"({"value": 42})";
    auto result = Parse(obj, json);
    
    return result && obj.value == 42;
}
static_assert(test_annotation_single_value_range(), 
    "[[=A<range<42, 42>>{}]] accepts exactly 42");

constexpr bool test_annotation_single_value_range_rejects() {
    struct Test {
        [[=A<range<42, 42>>{}]] int value;
    };
    
    Test obj{};
    std::string json = R"({"value": 43})";
    auto result = Parse(obj, json);
    
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_annotation_single_value_range_rejects(), 
    "[[=A<range<42, 42>>{}]] rejects 43");

// ============================================================================
// Test: Field without annotation (no validation)
// ============================================================================

constexpr bool test_annotation_plain_field() {
    struct Test {
        int unrestricted;  // No annotation - any value accepted
    };
    
    Test obj{};
    std::string json = R"({"unrestricted": 999999999})";
    auto result = Parse(obj, json);
    
    return result && obj.unrestricted == 999999999;
}
static_assert(test_annotation_plain_field(), 
    "Field without annotation accepts any value");

// ============================================================================
// Test: Mix of annotated and plain fields
// ============================================================================

constexpr bool test_annotation_mixed_fields() {
    struct Test {
        [[=A<range<0, 100>>{}]] int validated;
        int unvalidated;
        [[=A<range<-10, 10>>{}]] int also_validated;
    };
    
    Test obj{};
    std::string json = R"({"validated": 50, "unvalidated": 12345, "also_validated": -5})";
    auto result = Parse(obj, json);
    
    return result 
        && obj.validated == 50
        && obj.unvalidated == 12345
        && obj.also_validated == -5;
}
static_assert(test_annotation_mixed_fields(), 
    "Mix of annotated and plain fields works correctly");

// ============================================================================
// Test: Both C++26 annotations and A<>/Annotated<> work simultaneously
// ============================================================================

// Test: Mix of [[=A<...>{}]] and Annotated<> in same struct
constexpr bool test_both_syntaxes_valid() {
    struct Config {
        [[=A<range<0, 100>>{}]] int new_style;      // C++26 annotation
        Annotated<int, range<0, 100>> old_style;              // Traditional wrapper
        A<int, range<0, 100>> shorthand_style;                // A<> shorthand
        int plain;                                             // No validation
    };
    
    Config obj{};
    std::string json = R"({"new_style": 50, "old_style": 75, "shorthand_style": 25, "plain": 999})";
    auto result = Parse(obj, json);
    
    return result 
        && obj.new_style == 50
        && obj.old_style.get() == 75
        && obj.shorthand_style.get() == 25
        && obj.plain == 999;
}
static_assert(test_both_syntaxes_valid(), 
    "Both [[=A<...>{}]] and Annotated<>/A<> work in same struct");

// Test: C++26 annotation fails validation (others valid)
constexpr bool test_both_syntaxes_new_style_fails() {
    struct Config {
        [[=A<range<0, 100>>{}]] int new_style;
        Annotated<int, range<0, 100>> old_style;
    };
    
    Config obj{};
    std::string json = R"({"new_style": 150, "old_style": 50})";
    auto result = Parse(obj, json);
    
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_both_syntaxes_new_style_fails(), 
    "C++26 annotation validation failure detected in mixed struct");

// Test: Annotated<> fails validation (others valid)
constexpr bool test_both_syntaxes_old_style_fails() {
    struct Config {
        [[=A<range<0, 100>>{}]] int new_style;
        Annotated<int, range<0, 100>> old_style;
    };
    
    Config obj{};
    std::string json = R"({"new_style": 50, "old_style": 150})";
    auto result = Parse(obj, json);
    
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_both_syntaxes_old_style_fails(), 
    "Annotated<> validation failure detected in mixed struct");

// Test: A<> shorthand fails validation
constexpr bool test_both_syntaxes_shorthand_fails() {
    struct Config {
        [[=A<range<0, 100>>{}]] int new_style;
        A<int, range<0, 100>> shorthand;
    };
    
    Config obj{};
    std::string json = R"({"new_style": 50, "shorthand": -5})";
    auto result = Parse(obj, json);
    
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_both_syntaxes_shorthand_fails(), 
    "A<> shorthand validation failure detected in mixed struct");

// Test: Complex mixed struct with different validators
constexpr bool test_both_syntaxes_different_validators() {
    struct ServerConfig {
        [[=A<range<1, 65535>>{}]] int port;              // C++26: port range
        Annotated<int, range<1, 1000>> max_connections;            // Annotated<>
        A<int, range<1, 3600>> timeout_seconds;                    // A<> shorthand
        [[=A<range<0, 100>>{}]] int cpu_threshold;       // C++26: percentage
        int debug_level;                                            // No validation
    };
    
    ServerConfig obj{};
    std::string json = R"({
        "port": 8080,
        "max_connections": 500,
        "timeout_seconds": 300,
        "cpu_threshold": 80,
        "debug_level": 9999
    })";
    auto result = Parse(obj, json);
    
    return result 
        && obj.port == 8080
        && obj.max_connections.get() == 500
        && obj.timeout_seconds.get() == 300
        && obj.cpu_threshold == 80
        && obj.debug_level == 9999;
}
static_assert(test_both_syntaxes_different_validators(), 
    "Complex mixed struct with both syntaxes and different validators");

// ============================================================================
// Test: Non-POD types (C++26 reflection advantage over PFR)
// ============================================================================

// Non-POD structs defined at namespace scope to avoid overload ambiguity

// Non-POD: Has user-defined constructor (PFR can't handle this!)
struct NonPodWithConstructor {
    [[=A<range<0, 100>>{}]] int value;
    std::string name;
    
    constexpr NonPodWithConstructor() : value(0), name() {}
};

constexpr bool test_non_pod_with_constructor() {
    NonPodWithConstructor obj{};
    std::string json = R"({"value": 50, "name": "test"})";
    auto result = Parse(obj, json);
    
    return result && obj.value == 50 && obj.name == "test";
}
static_assert(test_non_pod_with_constructor(), 
    "C++26 reflection works with non-aggregate types (user-defined constructor)");

// Non-POD: Validation on type with constructor
struct NonPodForValidation {
    [[=A<range<0, 100>>{}]] int value;
    
    constexpr NonPodForValidation() : value(0) {}
};

constexpr bool test_non_pod_with_constructor_validation() {
    NonPodForValidation obj{};
    std::string json = R"({"value": 150})";
    auto result = Parse(obj, json);
    
    return !result && result.validationErrors().error() == SchemaError::number_out_of_range;
}
static_assert(test_non_pod_with_constructor_validation(), 
    "Validation works on non-aggregate types");

// Non-POD: Has default member initializers
struct NonPodWithDefaults {
    [[=A<range<1, 65535>>{}]] int port = 8080;
    int timeout = 30;
    
    constexpr NonPodWithDefaults() = default;
};

constexpr bool test_non_pod_with_default_values() {
    NonPodWithDefaults obj{};
    std::string json = R"({"port": 443, "timeout": 60})";
    auto result = Parse(obj, json);
    
    return result && obj.port == 443 && obj.timeout == 60;
}
static_assert(test_non_pod_with_default_values(), 
    "C++26 reflection works with default member initializers");

// Non-POD: Partial JSON (some fields keep defaults)
constexpr bool test_non_pod_partial_json() {
    NonPodWithDefaults obj{};
    std::string json = R"({"port": 443})";
    auto result = Parse(obj, json);
    
    return result && obj.port == 443 && obj.timeout == 30;
}
static_assert(test_non_pod_partial_json(), 
    "Non-POD type: unprovided fields keep their default values");

// Non-POD: Has methods (reflection only sees data members)
struct NonPodWithMethods {
    [[=A<range<0, 100>>{}]] int percentage;
    
    constexpr NonPodWithMethods() : percentage(0) {}
    
    // Methods don't interfere with reflection
    constexpr int doubled() const { return percentage * 2; }
    constexpr bool is_half() const { return percentage == 50; }
};

constexpr bool test_non_pod_with_methods() {
    NonPodWithMethods obj{};
    std::string json = R"({"percentage": 50})";
    auto result = Parse(obj, json);
    
    return result && obj.percentage == 50 && obj.doubled() == 100 && obj.is_half();
}
static_assert(test_non_pod_with_methods(), 
    "C++26 reflection ignores methods, only sees data members");

// ============================================================================
// Test: C-style arrays in structs (another PFR limitation)
// ============================================================================

// Simple 1D C array
struct WithCArray1D {
    [[=A<range<0, 100>>{}]] int values[3];
};

constexpr bool test_c_array_1d() {
    WithCArray1D obj{};
    std::string json = R"({"values": [10, 20, 30]})";
    auto result = Parse(obj, json);
    
    return result && obj.values[0] == 10 && obj.values[1] == 20 && obj.values[2] == 30;
}
static_assert(test_c_array_1d(), 
    "C++26 reflection works with 1D C-style arrays");

// 2D C array (nested)
struct WithCArray2D {
    int matrix[2][2];
};

constexpr bool test_c_array_2d() {
    WithCArray2D obj{};
    std::string json = R"({"matrix": [[1, 2], [3, 4]]})";
    auto result = Parse(obj, json);
    
    return result 
        && obj.matrix[0][0] == 1 && obj.matrix[0][1] == 2
        && obj.matrix[1][0] == 3 && obj.matrix[1][1] == 4;
}
static_assert(test_c_array_2d(), 
    "C++26 reflection works with 2D C-style arrays");

// Mixed: C array + other fields
struct WithMixedCArray {
    int id;
    [[=A<range<0, 255>>{}]] int rgb[3];
    std::string name;
};

constexpr bool test_c_array_mixed() {
    WithMixedCArray obj{};
    std::string json = R"({"id": 42, "rgb": [128, 64, 255], "name": "color"})";
    auto result = Parse(obj, json);
    
    // Note: rgb[2] = 255 is at boundary, should pass
    return result 
        && obj.id == 42
        && obj.rgb[0] == 128 && obj.rgb[1] == 64 && obj.rgb[2] == 255
        && obj.name == "color";
}
static_assert(test_c_array_mixed(), 
    "C++26 reflection works with mixed C-arrays and other fields");

// C array size validation (min_items/max_items)
struct WithCArraySizeValidation {
    [[=A<min_items<2>, max_items<4>>{}]] int values[4];
};

constexpr bool test_c_array_size_validation() {
    WithCArraySizeValidation obj{};
    // Provide exactly 3 items - within [2, 4] range
    std::string json = R"({"values": [1, 2, 3]})";
    auto result = Parse(obj, json);
    
    return result && obj.values[0] == 1 && obj.values[1] == 2 && obj.values[2] == 3;
}
static_assert(test_c_array_size_validation(), 
    "Array size validation works on C-style arrays");

// ============================================================================
// Test: Inheritance flattening — base class fields in flat JSON
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
// Test: Explicit composition flattening (options::flatten)
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
// Test: Flatten via wrapper type A<T, flatten> (non-C++26-annotation syntax)
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
