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
// Test: Non-POD types (only C++26 reflection can introspect non-aggregates)
// ============================================================================

// Non-POD structs defined at namespace scope to avoid overload ambiguity

// Non-POD: Has user-defined constructor (not an aggregate)
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
// Test: C-style arrays in structs (reflection-only introspection)
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

#endif // JSONFUSION_USE_REFLECTION
