#include "../test_helpers.hpp"
#include <JsonFusion/parser.hpp>
#include <JsonFusion/options.hpp>
#include <array>

using namespace TestHelpers;
using namespace JsonFusion;
using namespace JsonFusion::options;

// ============================================================================
// Test: Valid JSON Syntax (RFC 8259 Compliance)
// ============================================================================

// Test 1: Minimal valid JSON
struct Empty {};

static_assert(
    TestParse(R"({})", Empty{}),
    "Valid: empty object"
);

// Test 2: Deeply nested objects (10+ levels)
struct L10 { int x; };
struct L9 { L10 l10; };
struct L8 { L9 l9; };
struct L7 { L8 l8; };
struct L6 { L7 l7; };
struct L5 { L6 l6; };
struct L4 { L5 l5; };
struct L3 { L4 l4; };
struct L2 { L3 l3; };
struct L1 { L2 l2; };
struct Deep { L1 l1; };

static_assert(
    TestParse(R"({"l1": {"l2": {"l3": {"l4": {"l5": {"l6": {"l7": {"l8": {"l9": {"l10": {"x": 42}}}}}}}}}}})", 
        Deep{{{{{{{{{{42}}}}}}}}}}),
    "Valid: 10 levels of nesting"
);

// Test 3: Long field names
struct WithLongFieldName {
    int this_is_a_very_long_field_name_that_tests_how_the_parser_handles_extended_field_names;
};

static_assert(
    TestParse(R"({"this_is_a_very_long_field_name_that_tests_how_the_parser_handles_extended_field_names": 42})", 
        WithLongFieldName{42}),
    "Valid: long field name"
);

// Test 4: Many fields (20+)
struct ManyFields {
    int f1, f2, f3, f4, f5, f6, f7, f8, f9, f10;
    int f11, f12, f13, f14, f15, f16, f17, f18, f19, f20;
};

static_assert(
    TestParse(R"({
        "f1": 1, "f2": 2, "f3": 3, "f4": 4, "f5": 5,
        "f6": 6, "f7": 7, "f8": 8, "f9": 9, "f10": 10,
        "f11": 11, "f12": 12, "f13": 13, "f14": 14, "f15": 15,
        "f16": 16, "f17": 17, "f18": 18, "f19": 19, "f20": 20
    })", 
        ManyFields{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20}),
    "Valid: 20 fields"
);

// Test 5: Large arrays (100+ elements)
struct WithLargeArray {
    std::array<int, 100> values;
};

static_assert(
    TestParse<WithLargeArray>(R"({"values": [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99]})", 
        [](const WithLargeArray& obj) {
            for (std::size_t i = 0; i < 100; ++i) {
                if (obj.values[i] != static_cast<int>(i)) return false;
            }
            return true;
        }),
    "Valid: array with 100 elements"
);

// Test 6: All JSON value types
struct AllTypes {
    int number;
    bool flag;
    std::string text;
    std::optional<int> nullable;
    std::array<int, 3> array;
};

static_assert(
    TestParse(R"({
        "number": 42,
        "flag": true,
        "text": "hello",
        "nullable": null,
        "array": [1, 2, 3]
    })", 
        AllTypes{42, true, "hello", std::nullopt, {1, 2, 3}}),
    "Valid: all JSON value types"
);

// Test 7: Nested arrays
struct WithNestedArrays {
    std::array<std::array<int, 2>, 2> matrix;
};

static_assert(
    TestParse<WithNestedArrays>(R"({"matrix": [[1, 2], [3, 4]]})", 
        [](const WithNestedArrays& obj) {
            return obj.matrix[0][0] == 1 && obj.matrix[0][1] == 2
                && obj.matrix[1][0] == 3 && obj.matrix[1][1] == 4;
        }),
    "Valid: nested arrays"
);

// Test 8: Nested objects
struct NestedObj {
    int id;
    struct Inner {
        std::string name;
        int value;
    } inner;
};

static_assert(
    TestParse(R"({"id": 1, "inner": {"name": "test", "value": 42}})", 
        NestedObj{1, {"test", 42}}),
    "Valid: nested objects"
);

// Test 9: Empty arrays
struct WithEmptyArray {
    std::array<int, 0> empty;
    int other;
};

static_assert(
    TestParse(R"({"empty": [], "other": 7})",
        WithEmptyArray{{}, 7}),
    "Valid: empty array"
);

// Test 10: Single element arrays
struct WithSingleElement {
    std::array<int, 1> single;
};

static_assert(
    TestParse(R"({"single": [42]})", 
        WithSingleElement{{42}}),
    "Valid: single element array"
);

// Test 11: Compact JSON (no whitespace)
struct Compact {
    int a;
    int b;
    int c;
};

static_assert(
    TestParse(R"({"a":1,"b":2,"c":3})", Compact{1, 2, 3}),
    "Valid: compact JSON (no whitespace)"
);

// Test 12: Pretty-printed JSON (lots of whitespace)
static_assert(
    TestParse(R"({
        "a": 1,
        "b": 2,
        "c": 3
    })", Compact{1, 2, 3}),
    "Valid: pretty-printed JSON"
);

// Test 13: Empty key (RFC 8259 allows empty string keys)
struct WithEmptyKey {
    A<int, key<"">> empty_key_field;
    int normal_field;
};

static_assert(
    TestParse(R"({"": 42, "normal_field": 100})", 
        WithEmptyKey{42, 100}),
    "Valid: empty string key"
);

static_assert(
    TestParse(R"({"normal_field": 100, "": 42})", 
        WithEmptyKey{42, 100}),
    "Valid: empty string key (different order)"
);

