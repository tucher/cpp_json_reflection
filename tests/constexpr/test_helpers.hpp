#pragma once

#include <JsonFusion/parser.hpp>
#include <JsonFusion/serializer.hpp>
#include <string_view>
#include <cstddef>
#include <type_traits>
#include <utility>
// NOTE: no direct Boost.PFR dependency here. Struct comparison goes through
// JsonFusion's own introspection layer, which dispatches to Boost.PFR (C++20/23)
// or C++26 reflection automatically depending on the compiler/flags.

namespace TestHelpers {

// ============================================================================
// Parse Helpers
// ============================================================================

/// Check that parsing succeeds
template<typename T, typename Container>
constexpr bool ParseSucceeds(T& obj, const Container& json) {
    return static_cast<bool>(JsonFusion::Parse(obj, json));
}

/// Check that parsing succeeds with string_view
template<typename T>
constexpr bool ParseSucceeds(T& obj, std::string_view json) {
    return static_cast<bool>(JsonFusion::Parse(obj, json));
}

/// Check that parsing fails (any error)
template<typename T, typename Container>
constexpr bool ParseFails(T& obj, const Container& json) {
    return !JsonFusion::Parse(obj, json);
}

/// Check that parsing fails with specific error code
template<typename T, class ErrorT, typename Container>
constexpr bool ParseFailsWith(T& obj, const Container& json, ErrorT expected_error) {
    auto result = JsonFusion::Parse(obj, json);
    if constexpr(std::is_same_v<ErrorT, JsonFusion::ParseError>) {
        return !result && result.error() == expected_error;
    }else {
        return !result && result.readerError() == expected_error;
    }
}

/// Check that parsing fails with specific error code (string_view)
template<typename T, class ErrorT>
constexpr bool ParseFailsWith(T& obj, std::string_view json, ErrorT expected_error) {
    auto result = JsonFusion::Parse(obj, json);
    if constexpr(std::is_same_v<ErrorT, JsonFusion::ParseError>) {
        return !result && result.error() == expected_error;
    } else {
        return !result && result.readerError() == expected_error;
    }
}

template<typename T>
constexpr bool ParseFailsWithReaderError(T& obj, std::string_view json, JsonFusion::JsonIteratorReaderError expected_error) {
    auto result = JsonFusion::Parse(obj, json);
    return !result && result.readerError() == expected_error;
}

/// Check that parsing fails with specific error code at approximate position
/// Allows tolerance of +/- 2 characters for position
template<typename T, class ErrorT>
constexpr bool ParseFailsAt(T& obj, std::string_view json, 
                            ErrorT expected_error, 
                            std::size_t expected_pos_approx,
                            std::size_t tolerance = 2) {
    auto result = JsonFusion::Parse(obj, json);
    if (result) return false;  // Expected failure
    if constexpr(std::is_same_v<JsonFusion::ParseError, ErrorT>) {
        if (result.error() != expected_error) return false;  // Wrong error code
    } else {
        if (result.readerError() != expected_error) return false;
    }
    
    std::size_t actual_pos = result.pos() - json.data();
    return actual_pos >= (expected_pos_approx - tolerance) 
        && actual_pos <= (expected_pos_approx + tolerance);
}


// ============================================================================
// Round-Trip Helpers
// ============================================================================

/// Parse JSON, serialize back, compare byte-by-byte
template<typename T>
constexpr bool RoundTripEquals(T& obj, std::string_view original_json) {
    // Parse
    if (!JsonFusion::Parse(obj, original_json)) {
        return false;
    }
    
    std::string result;
    if (!JsonFusion::Serialize(obj, result)) {
        return false;
    }
    return result == original_json;
}



// ============================================================================
// String Comparison Helpers
// ============================================================================

/// Compare two char arrays for equality up to null terminator
template<std::size_t N1, std::size_t N2>
constexpr bool CStrEqual(const std::array<char, N1>& a, const std::array<char, N2>& b) {
    std::size_t i = 0;
    while (i < N1 && i < N2) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;  // Both reached null terminator
        ++i;
    }
    // If we're here, one array ended without null terminator match
    return false;
}

/// Compare char array with string literal
template<std::size_t N>
constexpr bool CStrEqual(const std::array<char, N>& arr, const char* str) {
    std::size_t i = 0;
    while (i < N && str[i] != '\0') {
        if (arr[i] != str[i]) return false;
        ++i;
    }
    return arr[i] == '\0' && str[i] == '\0';
}

/// Check if char array starts with given string
template<std::size_t N>
constexpr bool CStrStartsWith(const std::array<char, N>& arr, const char* prefix) {
    std::size_t i = 0;
    while (prefix[i] != '\0') {
        if (i >= N || arr[i] != prefix[i]) return false;
        ++i;
    }
    return true;
}

// ============================================================================
// Array Comparison Helpers
// ============================================================================

/// Compare two std::arrays element by element
template<typename T, std::size_t N>
constexpr bool ArrayEqual(const std::array<T, N>& a, const std::array<T, N>& b) {
    for (std::size_t i = 0; i < N; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

/// Compare array with initializer list
template<typename T, std::size_t N>
constexpr bool ArrayEqual(const std::array<T, N>& arr, const T (&expected)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        if (arr[i] != expected[i]) return false;
    }
    return true;
}

// ============================================================================
// Struct Comparison Helpers (backend-agnostic: Boost.PFR or C++26 reflection)
// ============================================================================

/// Compare two values field-by-field (constexpr-safe)
/// Handles nested structs, arrays, optionals, Annotated types
// Detect Annotated<T, ...>
template<class T>
struct is_annotated : std::false_type {};

template<class U, class... Opts>
struct is_annotated<JsonFusion::Annotated<U, Opts...>> : std::true_type {};

template<class T>
constexpr bool is_annotated_v = is_annotated<std::remove_cvref_t<T>>::value;


template<typename T>
constexpr bool DeepEqual(const T& a, const T& b) {
    // For Annotated<T> types, unwrap and compare underlying value
    if constexpr (is_annotated_v<T>) {
        return DeepEqual(a.get(), b.get());
    }
    // For primitive types, use operator==
    else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
        return a == b;
    }
    // For optionals
    else if constexpr (requires { a.has_value(); a.value(); }) {
        if (a.has_value() != b.has_value()) return false;
        if (!a.has_value()) return true;  // Both are nullopt
        return DeepEqual(a.value(), b.value());
    }
    // For unique_ptr
    else if constexpr (requires { a.get(); a.operator bool(); }) {
        bool a_null = (a.get() == nullptr);
        bool b_null = (b.get() == nullptr);
        if (a_null != b_null) return false;
        if (a_null) return true;  // Both are null
        return DeepEqual(*a, *b);
    }
    // For structs, compare field-by-field via JsonFusion's own introspection,
    // which dispatches to Boost.PFR (C++20/23) or C++26 reflection automatically.
    // Exclude begin/end types (e.g. std::array) so they take the container branch below.
    else if constexpr (std::is_aggregate_v<T> && !requires(const T& x) { x.begin(); x.end(); }) {
        constexpr std::size_t fields_count = JsonFusion::introspection::structureElementsCount<T>;
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (... && DeepEqual(
                JsonFusion::introspection::getStructElementByIndex<I>(a),
                JsonFusion::introspection::getStructElementByIndex<I>(b)));
        }(std::make_index_sequence<fields_count>{});
    }
    // For char arrays, only compare up to null terminator
    else if constexpr (requires { a.data(); a.size(); } && 
                      std::is_same_v<std::remove_cvref_t<decltype(*a.data())>, char>) {
        std::size_t i = 0;
        while (i < a.size()) {
            if (a[i] != b[i]) return false;
            if (a[i] == '\0') return true;  // Both reached null
            ++i;
        }
        return true;  // Both arrays fully matched
    }
    // For arrays/containers with elements
    else if constexpr (requires { a.begin(); a.end(); a.size(); }) {
        if (a.size() != b.size()) return false;
        auto it_a = a.begin();
        auto it_b = b.begin();
        while (it_a != a.end()) {
            if (!DeepEqual(*it_a, *it_b)) return false;
            ++it_a;
            ++it_b;
        }
        return true;
    }
    // Fallback to operator==
    else {
        return a == b;
    }
}

/// Compare two structs field-by-field
template<typename T>
constexpr bool StructEqual(const T& a, const T& b) {
    return DeepEqual(a, b);
}

/// Parse JSON and compare result with expected struct
/// Returns true if parsing succeeds AND all fields match
template<typename T>
constexpr bool ParseAndCompare(T& obj, std::string_view json, const T& expected) {
    if (!JsonFusion::Parse(obj, json)) {
        return false;
    }
    return DeepEqual(obj, expected);
}

/// Parse JSON and verify using custom comparison lambda
/// Useful when you only want to check specific fields
template<typename T, typename Comparator>
constexpr bool ParseAndVerify(T& obj, std::string_view json, Comparator&& cmp) {
    if (!JsonFusion::Parse(obj, json)) {
        return false;
    }
    return cmp(obj);
}

// ============================================================================
// Ultra-Minimal Test Helpers (Eliminate Boilerplate)
// ============================================================================

/// Short alias for std::string_view to reduce verbosity
/// Usage: json(R"({"x": 42})")
constexpr auto json = [](const char* s) constexpr { return std::string_view(s); };

/// One-line parse test: TestParse<Type>(json, expected)
/// Eliminates lambda boilerplate and object declaration
template<typename T>
constexpr bool TestParse(const char* json_str, const T& expected) {
    T obj{};
    return ParseAndCompare(obj, std::string_view(json_str), expected);
}

/// One-line parse test with custom verification
template<typename T, typename Verifier>
constexpr bool TestParse(const char* json_str, Verifier&& verify) {
    T obj{};
    return ParseAndVerify(obj, std::string_view(json_str), std::forward<Verifier>(verify));
}

/// One-line error test: TestParseError<Type>(json, error_code)
template<typename T, class ErrorClass>
constexpr bool TestParseError(const char* json_str, ErrorClass expected_error) {
    T obj{};
    if constexpr (std::is_same_v<ErrorClass, JsonFusion::ParseError>) {
        return ParseFailsWith(obj, std::string_view(json_str), expected_error);
    } else {
        return ParseFailsWithReaderError(obj, std::string_view(json_str), expected_error);
    }
}

/// One-line serialize test: TestSerialize(obj, expected_json)
template<typename T>
constexpr bool TestSerialize(const T& obj, std::string_view expected_json) {

    std::string result;
    if (!JsonFusion::Serialize(obj, result)) {
        return false;
    }

    return result == expected_json;
}

/// One-line round-trip test (byte-by-byte JSON comparison)
template<typename T>
constexpr bool TestRoundTrip(const char* json_str, const T& expected) {
    T obj{};
    return RoundTripEquals(obj, std::string_view(json_str));
}

/// Semantic round-trip test: Parse → Serialize → Parse → Compare objects
/// More robust than byte-by-byte comparison (handles whitespace/formatting differences)
template<typename T>
constexpr bool TestRoundTripSemantic(const char* json_str) {
    // Step 1: Parse original JSON
    T obj1{};
    if (!JsonFusion::Parse(obj1, std::string_view(json_str))) {
        return false;
    }
    
    // Step 2: Serialize to JSON string
    std::string serialized;
    if (!JsonFusion::Serialize(obj1, serialized)) {
        return false;
    }
    
    // Step 3: Parse serialized JSON
    T obj2{};
    if (!JsonFusion::Parse(obj2, serialized)) {
        return false;
    }
    
    // Step 4: Compare objects semantically
    return DeepEqual(obj1, obj2);
}

/// Semantic round-trip test with expected value verification
template<typename T>
constexpr bool TestRoundTripSemantic(const char* json_str, const T& expected) {
    // Step 1: Parse original JSON and verify it matches expected
    T obj1{};
    if (!JsonFusion::Parse(obj1, std::string_view(json_str))) {
        return false;
    }
    if (!DeepEqual(obj1, expected)) {
        return false;
    }
    
    // Step 2: Serialize to JSON string
    std::string serialized;
    if (!JsonFusion::Serialize(obj1, serialized)) {
        return false;
    }
    
    // Step 3: Parse serialized JSON
    T obj2{};
    if (!JsonFusion::Parse(obj2, serialized)) {
        return false;
    }
    
    // Step 4: Compare objects semantically
    return DeepEqual(obj1, obj2);
}

// ============================================================================
// JSON Path Helpers (for error tracking tests)
// ============================================================================

/// Check if path element matches expected field name (for struct fields)
template<std::size_t InlineKeyCapacity>
constexpr bool PathElementHasField(
    const JsonFusion::path::PathElement<InlineKeyCapacity>& elem,
    std::string_view expected_field) 
{
    return elem.field_name == expected_field;
}

/// Check if path element matches expected array index
template<std::size_t InlineKeyCapacity>
constexpr bool PathElementHasIndex(
    const JsonFusion::path::PathElement<InlineKeyCapacity>& elem,
    std::size_t expected_index) 
{
    return elem.array_index == expected_index;
}

/// Check if path element is a field (not array index)
template<std::size_t InlineKeyCapacity>
constexpr bool PathElementIsField(
    const JsonFusion::path::PathElement<InlineKeyCapacity>& elem) 
{
    return elem.array_index == std::numeric_limits<std::size_t>::max() 
        && !elem.field_name.empty();
}

/// Check if path element is an array index (not field)
template<std::size_t InlineKeyCapacity>
constexpr bool PathElementIsIndex(
    const JsonFusion::path::PathElement<InlineKeyCapacity>& elem) 
{
    return elem.array_index != std::numeric_limits<std::size_t>::max();
}

/// Compare path element with expected field name OR array index
/// Usage: PathElementMatches(elem, "field") or PathElementMatches(elem, 3)
template<std::size_t InlineKeyCapacity>
constexpr bool PathElementMatches(
    const JsonFusion::path::PathElement<InlineKeyCapacity>& elem,
    std::string_view expected_field) 
{
    return PathElementHasField(elem, expected_field);
}

template<std::size_t InlineKeyCapacity>
constexpr bool PathElementMatches(
    const JsonFusion::path::PathElement<InlineKeyCapacity>& elem,
    std::size_t expected_index) 
{
    return PathElementHasIndex(elem, expected_index);
}

/// Test parsing fails with specific error and JSON path depth
/// Example: TestParseErrorWithPathDepth<Config>(json, ParseError::..., 3)
template<typename T, class ErrorT>
constexpr bool TestParseErrorWithPathDepth(
    const char* json_str, 
    ErrorT expected_error,
    std::size_t expected_depth) 
{
    T obj{};
    auto result = JsonFusion::Parse(obj, std::string_view(json_str));
    if constexpr(std::is_same_v<ErrorT, JsonFusion::ParseError>) {
        return !result 
            && result.error() == expected_error
            && result.errorPath().currentLength == expected_depth;
    } else {
        return !result 
            && result.readerError() == expected_error
            && result.errorPath().currentLength == expected_depth;
    }
}

/// Test parsing fails with specific error and verify path element at given index
/// Example: TestParseErrorWithPathElement<Config>(json, error, 0, "field")
template<typename T, typename... PathCheck>
constexpr bool TestParseErrorWithPathElement(
    const char* json_str, 
    JsonFusion::ParseError expected_error,
    std::size_t element_index,
    PathCheck... checks) 
{
    T obj{};
    auto result = JsonFusion::Parse(obj, std::string_view(json_str));
    if (result || result.error() != expected_error) {
        return false;
    }
    
    const auto& path = result.errorPath();
    if (element_index >= path.currentLength) {
        return false;
    }
    
    // Check each path component
    return (... && PathElementMatches(path.storage[element_index], checks));
}

/// Test parsing fails and verify entire path chain
/// Usage: TestParseErrorWithPath<T>(json, error, "field1", 3, "field2")
/// This builds a path like $.field1[3].field2 and verifies it
template<typename T, class ErrorT, typename... PathComponents>
constexpr bool TestParseErrorWithPath(
    const char* json_str,
    ErrorT expected_error,
    PathComponents... expected_path)
{
    T obj{};
    auto result = JsonFusion::Parse(obj, std::string_view(json_str));
    if constexpr(std::is_same_v<JsonFusion::ParseError, ErrorT>) {
        if (result || result.error() != expected_error) {
            return false;
        }
    } else {
        if (result || result.readerError() != expected_error) {
            return false;
        }
    }
    
    const auto& path = result.errorPath();
    constexpr std::size_t expected_length = sizeof...(PathComponents);
    
    if (path.currentLength != expected_length) {
        return false;
    }
    
    // Verify each path element matches expected
    std::size_t index = 0;
    return (... && [&]() {
        bool matches = PathElementMatches(path.storage[index], expected_path);
        ++index;
        return matches;
    }());
}



// ============================================================================
// JSON Path Comparison (Generic, Type-Driven)
// ============================================================================

/// Compare two PathElement objects for equality
template<std::size_t InlineKeyCapacity>
constexpr bool PathElementsEqual(
    const JsonFusion::path::PathElement<InlineKeyCapacity>& a,
    const JsonFusion::path::PathElement<InlineKeyCapacity>& b)
{
    // Check array index
    if (a.array_index != b.array_index) {
        return false;
    }
    
    // Check field name
    if (a.field_name.size() != b.field_name.size()) {
        return false;
    }
    
    for (std::size_t i = 0; i < a.field_name.size(); ++i) {
        if (a.field_name[i] != b.field_name[i]) {
            return false;
        }
    }
    
    return true;
}

/// Compare two Path objects for equality
template<std::size_t SchemaDepth, bool SchemaHasMaps>
constexpr bool JsonPathsEqual(
    const JsonFusion::path::Path<SchemaDepth, SchemaHasMaps>& actual,
    const JsonFusion::path::Path<SchemaDepth, SchemaHasMaps>& expected)
{
    // 1. Check lengths are the same
    if (actual.currentLength != expected.currentLength) {
        return false;
    }
    
    // 2. Check each segment pair is equal
    for (std::size_t i = 0; i < actual.currentLength; ++i) {
        if (!PathElementsEqual(actual.storage[i], expected.storage[i])) {
            return false;
        }
    }
    
    return true;
}

/// Test parsing fails with specific error and expected JSON path
/// Uses type-driven Path construction for compile-time verification
/// Example: TestParseErrorWithJsonPath<Config>(json, ParseError::..., "field", 3, "nested")
template<typename T, class ErrorClass, typename... PathComponents>
constexpr bool TestParseErrorWithJsonPath(
    const char* json_str,
    ErrorClass expected_error,
    PathComponents... expected_path_components)
{
    T obj{};
    auto result = JsonFusion::Parse(obj, std::string_view(json_str));
    if constexpr(std::is_same_v<ErrorClass, JsonFusion::ParseError>) {
        if (result || result.error() != expected_error) {
            return false;
        }
    } else {
        if (result || result.readerError() != expected_error) {
            return false;
        }
    }
    
    // Calculate SchemaDepth and SchemaHasMaps for T (same as parser does)
    constexpr std::size_t SchemaDepth = JsonFusion::schema_analyzis::calc_type_depth<T>();
    constexpr bool SchemaHasMaps = JsonFusion::schema_analyzis::has_maps<T>();
    
    // Construct expected path directly from components
    using PathT = JsonFusion::path::Path<SchemaDepth, SchemaHasMaps>;
    PathT expected_path(expected_path_components...);
    
    // Compare paths
    return JsonPathsEqual(result.errorPath(), expected_path);
}

/// Test validation error with expected JSON path
template<typename T, typename... PathComponents>
constexpr bool TestValidationErrorWithJsonPath(
    const char* json_str,
    PathComponents... expected_path_components)
{
    T obj{};
    auto result = JsonFusion::Parse(obj, std::string_view(json_str));
    
    if (result || static_cast<bool>(result.validationErrors())) {
        return false;
    }
    
    // Calculate SchemaDepth and SchemaHasMaps for T
    constexpr std::size_t SchemaDepth = JsonFusion::schema_analyzis::calc_type_depth<T>();
    constexpr bool SchemaHasMaps = JsonFusion::schema_analyzis::has_maps<T>();
    
    // Construct expected path
    using PathT = JsonFusion::path::Path<SchemaDepth, SchemaHasMaps>;
    PathT expected_path(expected_path_components...);
    
    // Compare paths
    return JsonPathsEqual(result.errorPath(), expected_path);
}

} // namespace TestHelpers

