// ============================================================================
// UTF-8 validation of raw string bytes (RFC 8259 §8.1 / Unicode Table 3-7)
// ============================================================================
// JsonFusion validates that raw (unescaped) bytes inside JSON strings form
// well-formed UTF-8. This covers materialized string values, object keys, and
// skipped/unmodeled values. Rejects overlong encodings, UTF-16 surrogates
// (U+D800..U+DFFF) and code points > U+10FFFF. The \uXXXX escape path is
// validated separately (surrogate pairing) and always produces valid UTF-8.
//
// These double as a constexpr "adversarial corpus": because they are evaluated
// at compile time, an out-of-bounds/overflow regression in the scanner would
// fail to compile, and a behavior regression flips the expected error code.
// ============================================================================

#include "../test_helpers.hpp"
#include <JsonFusion/parser.hpp>
#include <JsonFusion/options.hpp>
#include <string>
#include <string_view>

using namespace JsonFusion;
using namespace JsonFusion::options;
using namespace TestHelpers;
using JsonFusion::JsonIteratorReaderError;

namespace {

struct S { std::string s; };

constexpr bool ok(std::string_view json) {
    S o{};
    return ParseSucceeds(o, json);
}
constexpr bool bad_utf8(std::string_view json) {
    S o{};
    return ParseFailsWithReaderError(o, json, JsonIteratorReaderError::INVALID_UTF8);
}

// ---- Valid UTF-8, including range boundaries (must be accepted) ------------
static_assert(ok("{\"s\":\"ascii only\"}"),                 "ASCII");
static_assert(ok("{\"s\":\"\xC2\x80\"}"),                   "U+0080 smallest 2-byte");
static_assert(ok("{\"s\":\"\xDF\xBF\"}"),                   "U+07FF largest 2-byte");
static_assert(ok("{\"s\":\"caf\xC3\xA9\"}"),                "e-acute (C3 A9)");
static_assert(ok("{\"s\":\"\xE0\xA0\x80\"}"),               "U+0800 smallest 3-byte");
static_assert(ok("{\"s\":\"\xE2\x82\xAC\"}"),               "euro sign (E2 82 AC)");
static_assert(ok("{\"s\":\"\xEF\xBF\xBF\"}"),               "U+FFFF largest 3-byte");
static_assert(ok("{\"s\":\"\xF0\x90\x80\x80\"}"),           "U+10000 smallest 4-byte");
static_assert(ok("{\"s\":\"\xF0\x9F\x98\x80\"}"),           "emoji (F0 9F 98 80)");
static_assert(ok("{\"s\":\"\xF4\x8F\xBF\xBF\"}"),           "U+10FFFF largest valid");
// \uXXXX escape path still works and is unaffected
static_assert(ok("{\"s\":\"caf\\u00e9\"}"),                 "escaped U+00E9");
static_assert(ok("{\"s\":\"\\uD83D\\uDE00\"}"),             "escaped surrogate pair (emoji)");

// ---- Overlong encodings (must be rejected) --------------------------------
static_assert(bad_utf8("{\"s\":\"\xC0\x80\"}"),             "overlong C0 80 (NUL)");
static_assert(bad_utf8("{\"s\":\"\xC1\xBF\"}"),             "overlong C1");
static_assert(bad_utf8("{\"s\":\"\xE0\x9F\xBF\"}"),         "overlong 3-byte (E0 9F..)");
static_assert(bad_utf8("{\"s\":\"\xF0\x8F\xBF\xBF\"}"),     "overlong 4-byte (F0 8F..)");

// ---- Stray continuation / bad lead bytes ----------------------------------
static_assert(bad_utf8("{\"s\":\"\x80\"}"),                 "lone continuation 0x80");
static_assert(bad_utf8("{\"s\":\"\xBF\"}"),                 "lone continuation 0xBF");
static_assert(bad_utf8("{\"s\":\"\xF5\x80\x80\x80\"}"),     "F5 lead (> plane 16)");
static_assert(bad_utf8("{\"s\":\"\xFF\"}"),                 "0xFF is never valid");

// ---- Truncated sequences (lead byte then closing quote) -------------------
static_assert(bad_utf8("{\"s\":\"\xC2\"}"),                 "truncated 2-byte");
static_assert(bad_utf8("{\"s\":\"\xE2\x82\"}"),             "truncated 3-byte");
static_assert(bad_utf8("{\"s\":\"\xF0\x9F\x98\"}"),         "truncated 4-byte");
// lead byte followed by a non-continuation byte (ASCII)
static_assert(bad_utf8("{\"s\":\"\xC3z\"}"),                "lead + ASCII");

// ---- UTF-16 surrogates encoded as raw UTF-8 (must be rejected) ------------
static_assert(bad_utf8("{\"s\":\"\xED\xA0\x80\"}"),         "raw U+D800 (high surrogate)");
static_assert(bad_utf8("{\"s\":\"\xED\xBF\xBF\"}"),         "raw U+DFFF (low surrogate)");

// ---- Beyond U+10FFFF (must be rejected) -----------------------------------
static_assert(bad_utf8("{\"s\":\"\xF4\x90\x80\x80\"}"),     "U+110000 (> U+10FFFF)");

// ---- Object keys (read through the same string machinery) -----------------
constexpr bool bad_key(std::string_view json) {
    S o{};  // invalid key is validated during the key read, before matching
    return ParseFailsWithReaderError(o, json, JsonIteratorReaderError::INVALID_UTF8);
}
static_assert(bad_key("{\"\xC0\x80\":1}"),                  "invalid UTF-8 in key");
static_assert(bad_key("{\"\xED\xA0\x80\":1}"),              "surrogate in key");

// ---- Skipped / unmodeled values (skip path) -------------------------------
struct Req { int required; };
constexpr bool bad_skipped(std::string_view json) {
    Annotated<Req, allow_excess_fields> o{};
    return ParseFailsWithReaderError(o, json, JsonIteratorReaderError::INVALID_UTF8);
}
constexpr bool ok_skipped(std::string_view json) {
    Annotated<Req, allow_excess_fields> o{};
    return ParseSucceeds(o, json);
}
static_assert(ok_skipped("{\"required\":1,\"x\":\"caf\xC3\xA9\"}"),        "valid UTF-8 in skipped string");
static_assert(bad_skipped("{\"required\":1,\"x\":\"\xC0\x80\"}"),          "invalid UTF-8 in skipped string");
static_assert(bad_skipped("{\"required\":1,\"x\":{\"y\":\"\xED\xA0\x80\"}}"), "invalid UTF-8 in skipped nested object");
static_assert(bad_skipped("{\"required\":1,\"x\":[\"ok\",\"\xF4\x90\x80\x80\"]}"), "invalid UTF-8 in skipped array element");

// ---- Serialization: JsonFusion emits only valid UTF-8 (round-trip symmetry) ----
// Producing invalid UTF-8 would make the output non-conforming JSON (RFC 8259
// §8.1), so a string field holding ill-formed bytes fails to serialize instead.
constexpr bool ser_ok(std::string_view content) {
    S o{}; o.s = std::string(content);
    std::string out;
    return static_cast<bool>(Serialize(o, out));
}
constexpr bool ser_bad(std::string_view content) {
    S o{}; o.s = std::string(content);
    std::string out;
    auto r = Serialize(o, out);
    return !r && r.writerError() == JsonFusion::JsonIteratorWriterError::INVALID_UTF8;
}
static_assert(ser_ok("caf\xC3\xA9"),          "valid 2-byte serializes");
static_assert(ser_ok("\xF0\x9F\x98\x80"),     "valid 4-byte serializes");
static_assert(ser_bad("\xC0\x80"),            "overlong rejected on serialize");
static_assert(ser_bad("\x80"),                "lone continuation rejected on serialize");
static_assert(ser_bad("\xC2"),                "truncated (at end) rejected on serialize");
static_assert(ser_bad("\xC2z"),               "lead + ASCII rejected on serialize");
static_assert(ser_bad("\xED\xA0\x80"),        "raw surrogate rejected on serialize");
static_assert(ser_bad("\xF4\x90\x80\x80"),    "out-of-range rejected on serialize");

} // namespace
