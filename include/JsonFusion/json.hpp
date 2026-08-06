#pragma once

#include <limits>
#include <string_view>

#include "fp_to_str.hpp"
#include "reader_concept.hpp"
#include "writer_concept.hpp"
#include <cmath>
#include <cstring>
#include <cstdint>

namespace JsonFusion {

// ── Table-free incremental UTF-8 validator (Unicode Table 3-7) ─────────────
// Rejects overlong encodings, UTF-16 surrogates (U+D800..U+DFFF) and code
// points > U+10FFFF. No lookup table, so it stays cheap on constrained targets.
namespace utf8_detail {
    inline constexpr std::uint32_t UTF8_ACCEPT = 0;
    inline constexpr std::uint32_t UTF8_REJECT = 0xFFFFFFFFu;

    constexpr std::uint32_t utf8_pack(std::uint32_t need, unsigned lo, unsigned hi) {
        return (need << 16) | (static_cast<std::uint32_t>(lo) << 8) | static_cast<std::uint32_t>(hi);
    }

    // Feed one byte. UTF8_ACCEPT means no partial multibyte sequence is pending;
    // any other (non-REJECT) value encodes (bytes_left, lo, hi) for the next byte.
    constexpr std::uint32_t utf8_step(std::uint32_t state, unsigned char b) {
        if (state == UTF8_ACCEPT) {
            if (b < 0x80u)  return UTF8_ACCEPT;                 // ASCII
            if (b < 0xC2u)  return UTF8_REJECT;                 // stray continuation / overlong lead (C0,C1)
            if (b < 0xE0u)  return utf8_pack(1, 0x80u, 0xBFu);  // C2-DF
            if (b == 0xE0u) return utf8_pack(2, 0xA0u, 0xBFu);  // exclude overlong
            if (b < 0xEDu)  return utf8_pack(2, 0x80u, 0xBFu);  // E1-EC
            if (b == 0xEDu) return utf8_pack(2, 0x80u, 0x9Fu);  // exclude surrogates
            if (b < 0xF0u)  return utf8_pack(2, 0x80u, 0xBFu);  // EE-EF
            if (b == 0xF0u) return utf8_pack(3, 0x90u, 0xBFu);  // exclude overlong
            if (b < 0xF4u)  return utf8_pack(3, 0x80u, 0xBFu);  // F1-F3
            if (b == 0xF4u) return utf8_pack(3, 0x80u, 0x8Fu);  // exclude > U+10FFFF
            return UTF8_REJECT;                                 // F5-FF
        }
        if (state == UTF8_REJECT) return UTF8_REJECT;
        const std::uint32_t need = state >> 16;
        const unsigned lo = (state >> 8) & 0xFFu;
        const unsigned hi = state & 0xFFu;
        if (b < lo || b > hi) return UTF8_REJECT;
        if (need == 1) return UTF8_ACCEPT;
        return utf8_pack(need - 1u, 0x80u, 0xBFu);
    }
}

enum class JsonIteratorReaderError {
    NO_ERROR,
    UNEXPECTED_END_OF_DATA,
    EXCESS_CHARACTERS,
    ILLFORMED_NULL,
    ILLFORMED_BOOL,
    ILLFORMED_OBJECT,
    ILLFORMED_STRING,
    ILLFORMED_NUMBER,
    ILLFORMED_ARRAY,
    WIRE_SINK_OVERFLOW,
    SKIPPING_STACK_OVERFLOW,
    NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE,
    INVALID_UTF8
};

template<class It, class Sent, std::size_t MaxSkipNesting = 64>
class JsonIteratorReader {
public:
    using iterator_type = It;
    struct ArrayFrame {};
    struct MapFrame {};

  

    using error_type = JsonIteratorReaderError;

    constexpr JsonIteratorReader(It first, Sent last)
        : m_error(JsonIteratorReaderError::NO_ERROR), current_(first), end_(last) {}



    __attribute__((noinline)) constexpr  reader::TryParseStatus start_value_and_try_read_null() {
        while (current_ != end_ && isSpace(*current_)) {
            ++current_;
        }
        if(atEnd())  {
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            return reader::TryParseStatus::error;
        }

        if (*current_ != 'n') {
            return reader::TryParseStatus::no_match;
        }
        current_ ++;
        if (!match_literal("ull")) {
            setError(JsonIteratorReaderError::ILLFORMED_NULL);
            return reader::TryParseStatus::error;
        }
        return reader::TryParseStatus::ok;
    }

    __attribute__((noinline)) constexpr  reader::TryParseStatus read_bool(bool & b) {
        if(atEnd())  {
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            return reader::TryParseStatus::error;
        }
        switch(*current_) {
        case 't': {
            current_ ++;
            if (match_literal("rue"))  {
                if(isPlainEnd(*current_) || atEnd()){
                    b = true;
                    return reader::TryParseStatus::ok;
                }
            }
            setError(JsonIteratorReaderError::ILLFORMED_BOOL);
            return reader::TryParseStatus::error;
        }
        case 'f': {
            current_ ++;
            if (match_literal("alse"))  {
                if (isPlainEnd(*current_) || atEnd()){
                    b = false;
                    return reader::TryParseStatus::ok;
                }
            }
            setError(JsonIteratorReaderError::ILLFORMED_BOOL);
            return reader::TryParseStatus::error;
        }
        default:
            return reader::TryParseStatus::no_match;
        }
    }
    __attribute__((noinline)) constexpr  bool finish() {
        while (current_ != end_ && isSpace(*current_)) {
            ++current_;
        }
        if (current_ != end_) {
            setError(JsonIteratorReaderError::EXCESS_CHARACTERS);
            return false;
        }
        return true;
    }
    
    // WireSink support - capture raw JSON value into sink
    template<WireSinkLike Sink>
    constexpr bool capture_to_sink(Sink& sink) {
        sink.clear();
        WireSinkFiller<Sink> filler{&sink, false};
        
        if (!skip_json_value_internal<MaxSkipNesting>(filler)) {
            if (filler.overflow) {
                setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
            }
            // Error already set by skip_json_value_internal if not overflow
            return false;
        }
        
        return true;
    }


    // Array/object structural events
    __attribute__((noinline)) constexpr  reader::IterationStatus read_array_begin(ArrayFrame&) {
        reader::IterationStatus ret;
        if(*current_ != '[')  {
            ret.status = reader::TryParseStatus::no_match;
            return ret;
        }
        current_++;
        if(!skip_whitespace()) {
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(atEnd())  {
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(*current_ == ',') {
            setError(JsonIteratorReaderError::ILLFORMED_ARRAY);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(*current_ != ']') {
            ret.has_value = true;
            if(atEnd())  {
                setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                ret.status = reader::TryParseStatus::error;
                return ret;
            }
        } else {
            current_++;

        }
        if(!skip_whitespace()) {
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        ret.status = reader::TryParseStatus::ok;
        return ret;
    }
    __attribute__((noinline)) constexpr  reader::IterationStatus read_map_begin(MapFrame&) {
        reader::IterationStatus ret;
        if(*current_ != '{')  {
            ret.status = reader::TryParseStatus::no_match;
            return ret;
        }
        current_++;
        if(!skip_whitespace()) {
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(atEnd())  {
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(*current_ == ',') {
            setError(JsonIteratorReaderError::ILLFORMED_OBJECT);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(*current_ != '}') {
            ret.has_value = true;
            if(atEnd())  {
                setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                ret.status = reader::TryParseStatus::error;
                return ret;
            }
        } else {
            current_++;

        }
        if(!skip_whitespace()) {
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        ret.status = reader::TryParseStatus::ok;
        return ret;
    }


    // Comma handling
    __attribute__((noinline)) constexpr  reader::IterationStatus advance_after_value(ArrayFrame&) {
        reader::IterationStatus ret;
        if(!skip_whitespace())  {
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(*current_ == ']')  {
            current_ ++;
            ret.has_value = false;
            ret.status = reader::TryParseStatus::ok;
            return ret;
        }
        if(*current_ == ',')  {
            current_ ++;
        } else {
            setError(JsonIteratorReaderError::ILLFORMED_ARRAY);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(!skip_whitespace()) {
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(*current_==',' || *current_ == ']') {
            setError(JsonIteratorReaderError::ILLFORMED_ARRAY);
            ret.status = reader::TryParseStatus::error;
        } else {
            ret.has_value = true;
            ret.status = reader::TryParseStatus::ok;
        }
        return ret;
    }
    __attribute__((noinline)) constexpr reader::IterationStatus advance_after_value(MapFrame&) {
        reader::IterationStatus ret{};

        // After finishing a value, move to either '}' or ','.
        skip_whitespace();

        if (atEnd()) {
            // Object cannot validly end right after a value without '}'.
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        char c = *current_;

        // Case 1: end of object: ... "value" }
        if (c == '}') {
            ++current_;
            ret.has_value = false;
            ret.status    = reader::TryParseStatus::ok;
            return ret;
        }

        // Case 2: must be a comma separating members: ... "value" ,
        if (c != ',') {
            setError(JsonIteratorReaderError::ILLFORMED_OBJECT);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        // Consume the comma and skip whitespace before the next key.
        ++current_;
        skip_whitespace();

        if (atEnd()) {
            // Trailing comma: { "a": 1, <EOF> }
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        // In JSON objects, next non-WS must be a string key.
        if (*current_ != '"') {
            // This also catches { "a": 1, } and { "a": 1, , ... }
            setError(JsonIteratorReaderError::ILLFORMED_OBJECT);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        // We’re positioned on the next key.
        ret.has_value = true;
        ret.status    = reader::TryParseStatus::ok;
        return ret;
    }
    __attribute__((noinline)) constexpr  bool move_to_value(MapFrame&) {
        if(!skip_whitespace()) {
            return false;
        }
        if(*current_ != ':') {
            setError(JsonIteratorReaderError::ILLFORMED_OBJECT);
            return false;
        }
        current_ ++;
        if(!skip_whitespace()) {
            return false;
        }
        return true;
    }

    // Value parsing is handled by higher-level ParseValue(T&, Reader&, Ctx&),
    // so reader doesn’t know types; it just supplies char-level ops.
    __attribute__((noinline)) constexpr  It current() const { return current_; }
    __attribute__((noinline)) constexpr  Sent end()  const { return end_; }




    template<class NumberT>
    __attribute__((noinline)) constexpr  reader::TryParseStatus read_number(NumberT & storage) {
        char buf[fp_to_str_detail::NumberBufSize];
        std::size_t index = 0;
        bool seenDot = false;
        bool seenExp = false;

        if (!read_number_token(buf, index, seenDot, seenExp)) {
            // setError(JsonIteratorReaderError::WRONG_JSON_FOR_NUMBER_STORAGE);
            return reader::TryParseStatus::error;
        }

        if constexpr (std::is_integral_v<NumberT>) {
            // Reject decimals/exponents for integer fields
            if (seenDot || seenExp) {
                return reader::TryParseStatus::no_match;
            }

            NumberT value{};
            if(!parse_decimal_integer<NumberT>(buf, value)) {
                setError(JsonIteratorReaderError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                return reader::TryParseStatus::error;
            }

            storage = value;
            return reader::TryParseStatus::ok;
        } else if constexpr (std::is_floating_point_v<NumberT>) {
            double x;
            if(fp_to_str_detail::parse_number_to_double(buf, x)){
                if(static_cast<double>(std::numeric_limits<NumberT>::lowest()) > x
                    || static_cast<double>(std::numeric_limits<NumberT>::max()) < x){
                    setError(JsonIteratorReaderError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                    return reader::TryParseStatus::error;
                }
                storage = static_cast<NumberT>(x);
                return reader::TryParseStatus::ok;
            } else {
                setError(JsonIteratorReaderError::ILLFORMED_NUMBER);
                return reader::TryParseStatus::error;
            }
        } else {
            // Should never happen if NumberLike is correct
            static_assert(std::is_integral_v<NumberT> || std::is_floating_point_v<NumberT>,
                          "[[[ JsonFusion ]]] NumberLike underlying type must be integral or floating");
            return reader::TryParseStatus::error;
        }

    }
    template<class NumberT>
    constexpr bool read_key_as_index(NumberT & out) {
        constexpr std::size_t bufSize = 32;
        static_assert(bufSize >= std::numeric_limits<NumberT>::digits10 + 3,
                      "read_key_as_index buffer too small for NumberT");
        char buf[bufSize];
        if(reader::StringChunkResult r = read_string_chunk(buf, bufSize-1); !r.done || r.status != reader::StringChunkStatus::ok) {
            return false;
        } else {
            buf[r.bytes_written] = 0;
            if(!parse_decimal_integer<NumberT>(buf, out)) {
                setError(JsonIteratorReaderError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                return false;
            } else {
                return true;
            }
        }
    }

    __attribute__((noinline))
    constexpr reader::StringChunkResult read_string_chunk(char* out, std::size_t capacity) {
        std::size_t written = 0;

        // If we're not currently inside a string, expect an opening quote.
        if (!in_string_) {
            if (atEnd()) {
                setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                return {reader::StringChunkStatus::error, 0, false};
            }
            if (*current_ != '"') {
                return {reader::StringChunkStatus::no_match, 0, false};
            }
            in_string_ = true;
            utf8_state_ = utf8_detail::UTF8_ACCEPT;
            ++current_; // consume opening '"'
        }

        auto handle_capacity_full = [&](std::size_t written_bytes) {
            // If buffer is full, but the next char is the closing quote,
            // we can still finish the string in this call.
            if (!atEnd() && *current_ == '"') {
                if (utf8_state_ != utf8_detail::UTF8_ACCEPT) {
                    setError(JsonIteratorReaderError::INVALID_UTF8);
                    in_string_ = false; string_buf_len_ = 0; string_buf_pos_ = 0;
                    return reader::StringChunkResult{reader::StringChunkStatus::error, written_bytes, false};
                }
                ++current_;
                in_string_      = false;
                string_buf_len_ = 0;
                string_buf_pos_ = 0;
                return reader::StringChunkResult{
                    reader::StringChunkStatus::ok,
                    written_bytes,
                    true // done
                };
            }
            // Otherwise, caller continues the string later.
            return reader::StringChunkResult{
                reader::StringChunkStatus::ok,
                written_bytes,
                false // not done
            };
        };

        // First, flush any leftover UTF-8 bytes from previous escapes.
        if (string_buf_pos_ < string_buf_len_) {
            while (string_buf_pos_ < string_buf_len_ && written < capacity) {
                out[written++] = string_buf_[string_buf_pos_++];
            }

            if (string_buf_pos_ == string_buf_len_) {
                string_buf_pos_ = 0;
                string_buf_len_ = 0;
            }

            if (written == capacity) {
                return handle_capacity_full(written);
            }
        }

        // Main loop: fill caller's buffer up to capacity, or until string ends / error.
        while (written < capacity) {
            if (atEnd()) {
                setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                in_string_      = false;
                string_buf_len_ = 0;
                string_buf_pos_ = 0;
                return {reader::StringChunkStatus::error, written, false};
            }

            // ---- Fast path: copy a run of non-special chars ----
            while (written < capacity && !atEnd()) {
                char c  = *current_;
                auto uc = static_cast<unsigned char>(c);

                // Stop on: closing quote, backslash (escape), or control char.
                if (c == '"' || c == '\\' || uc <= 0x1F) {
                    break;
                }

                // Validate raw UTF-8 incrementally (state persists across chunks).
                utf8_state_ = utf8_detail::utf8_step(utf8_state_, uc);
                if (utf8_state_ == utf8_detail::UTF8_REJECT) {
                    setError(JsonIteratorReaderError::INVALID_UTF8);
                    in_string_ = false; string_buf_len_ = 0; string_buf_pos_ = 0;
                    return {reader::StringChunkStatus::error, written, false};
                }

                out[written++] = c;
                ++current_;
            }

            if (written == capacity) {
                return handle_capacity_full(written);
            }

            if (atEnd()) {
                setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                in_string_      = false;
                string_buf_len_ = 0;
                string_buf_pos_ = 0;
                return {reader::StringChunkStatus::error, written, false};
            }

            char c = *current_;

            // A special char (quote / backslash / control) is a non-continuation
            // byte: if a multibyte UTF-8 sequence was still pending, it's truncated.
            if (utf8_state_ != utf8_detail::UTF8_ACCEPT) {
                setError(JsonIteratorReaderError::INVALID_UTF8);
                in_string_ = false; string_buf_len_ = 0; string_buf_pos_ = 0;
                return {reader::StringChunkStatus::error, written, false};
            }

            // ---- Slow path: special characters ----

            // 1) End of string
            if (c == '"') {
                ++current_;
                in_string_      = false;
                string_buf_len_ = 0;
                string_buf_pos_ = 0;
                return {reader::StringChunkStatus::ok, written, true};
            }

            // 2) Escape sequence
            if (c == '\\') {
                ++current_;
                if (atEnd()) {
                    setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                    in_string_ = false;
                    return {reader::StringChunkStatus::error, written, false};
                }

                char esc = *current_;
                ++current_;

                // Simple 1-byte escapes
                char simple = 0;
                switch (esc) {
                case '"':  simple = '"';  break;
                case '/':  simple = '/';  break;
                case '\\': simple = '\\'; break;
                case 'b':  simple = '\b'; break;
                case 'f':  simple = '\f'; break;
                case 'r':  simple = '\r'; break;
                case 'n':  simple = '\n'; break;
                case 't':  simple = '\t'; break;
                case 'u':  break; // handled below
                default:
                    setError(JsonIteratorReaderError::ILLFORMED_STRING);
                    in_string_ = false;
                    return {reader::StringChunkStatus::error, written, false};
                }

                // Simple escape: write directly or buffer if no space left.
                if (esc != 'u') {
                    if (written < capacity) {
                        out[written++] = simple;
                    } else {
                        string_buf_pos_ = 0;
                        string_buf_len_ = 1;
                        string_buf_[0]  = simple;
                        return handle_capacity_full(written);
                    }
                    continue;
                }

                // ---- \uXXXX handling ----
                std::uint16_t u1 = 0;
                if (!readHex4(u1)) {
                    in_string_ = false;
                    return {reader::StringChunkStatus::error, written, false};
                }

                std::uint32_t codepoint = 0;

                if (u1 >= 0xD800u && u1 <= 0xDBFFu) {
                    // High surrogate, expect a second \uXXXX
                    if (atEnd()) {
                        setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                        in_string_ = false;
                        return {reader::StringChunkStatus::error, written, false};
                    }
                    if (*current_ != '\\') {
                        setError(JsonIteratorReaderError::ILLFORMED_STRING);
                        in_string_ = false;
                        return {reader::StringChunkStatus::error, written, false};
                    }
                    ++current_;
                    if (atEnd()) {
                        setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                        in_string_ = false;
                        return {reader::StringChunkStatus::error, written, false};
                    }
                    if (*current_ != 'u') {
                        setError(JsonIteratorReaderError::ILLFORMED_STRING);
                        in_string_ = false;
                        return {reader::StringChunkStatus::error, written, false};
                    }
                    ++current_;

                    std::uint16_t u2 = 0;
                    if (!readHex4(u2)) {
                        in_string_ = false;
                        return {reader::StringChunkStatus::error, written, false};
                    }
                    if (u2 < 0xDC00u || u2 > 0xDFFFu) {
                        setError(JsonIteratorReaderError::ILLFORMED_STRING);
                        in_string_ = false;
                        return {reader::StringChunkStatus::error, written, false};
                    }

                    codepoint = 0x10000u
                                + ((static_cast<std::uint32_t>(u1) - 0xD800u) << 10)
                                + (static_cast<std::uint32_t>(u2) - 0xDC00u);
                } else if (u1 >= 0xDC00u && u1 <= 0xDFFFu) {
                    // Lone low surrogate → invalid
                    setError(JsonIteratorReaderError::ILLFORMED_STRING);
                    in_string_ = false;
                    return {reader::StringChunkStatus::error, written, false};
                } else {
                    codepoint = u1;
                }

                // Encode codepoint as UTF-8 into a small local buffer,
                // then write as much as fits, buffer the rest (if any).
                char utf8[4];
                std::size_t utf8_len = 0;

                if (codepoint <= 0x7Fu) {
                    utf8[utf8_len++] = static_cast<char>(codepoint);
                } else if (codepoint <= 0x7FFu) {
                    utf8[utf8_len++] = static_cast<char>(0xC0 | (codepoint >> 6));
                    utf8[utf8_len++] = static_cast<char>(0x80 | (codepoint & 0x3F));
                } else if (codepoint <= 0xFFFFu) {
                    utf8[utf8_len++] = static_cast<char>(0xE0 | (codepoint >> 12));
                    utf8[utf8_len++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    utf8[utf8_len++] = static_cast<char>(0x80 | (codepoint & 0x3F));
                } else { // up to 0x10FFFF
                    utf8[utf8_len++] = static_cast<char>(0xF0 | (codepoint >> 18));
                    utf8[utf8_len++] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                    utf8[utf8_len++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    utf8[utf8_len++] = static_cast<char>(0x80 | (codepoint & 0x3F));
                }

                // Write as many bytes as we can to out, buffer the remainder.
                std::size_t i = 0;
                while (i < utf8_len && written < capacity) {
                    out[written++] = utf8[i++];
                }

                if (i < utf8_len) {
                    // Not all bytes fit; store the rest in string_buf_ for next call.
                    string_buf_pos_ = 0;
                    string_buf_len_ = utf8_len - i;
                    for (std::size_t j = 0; j < string_buf_len_; ++j) {
                        string_buf_[j] = utf8[i + j];
                    }
                    return handle_capacity_full(written);
                }

                continue;
            }

            // 3) Control characters (<= 0x1F) must be escaped.
            if (static_cast<unsigned char>(c) <= 0x1F) {
                setError(JsonIteratorReaderError::ILLFORMED_STRING);
                in_string_      = false;
                string_buf_len_ = 0;
                string_buf_pos_ = 0;
                return {reader::StringChunkStatus::error, written, false};
            }

            // Should not reach here: fast path handles normal chars,
            // special cases are handled above.
        }

        // Out of capacity, still inside string; caller will resume.
        return handle_capacity_full(written);
    }

    __attribute__((noinline)) constexpr bool skip_value() {
        NoOpFiller filler{};
        return skip_json_value_internal<MaxSkipNesting>(filler);
    }
    __attribute__((noinline)) constexpr JsonIteratorReaderError getError() const {
        return m_error;
    }

    static constexpr auto from_sink(const WireSinkLike auto & sink) {
        return JsonIteratorReader<const char*, const char*, MaxSkipNesting>(sink.data(), sink.data() + sink.current_size());
    }
private:
    constexpr  bool read_number_token(char (&buf)[fp_to_str_detail::NumberBufSize],
                                     std::size_t& index,
                                     bool& seenDot,
                                     bool& seenExp)
    {
        index   = 0;
        seenDot = false;
        seenExp = false;

        bool inExp            = false;
        bool seenDigitBeforeExp = false;
        bool seenDigitAfterExp  = false;
        bool firstDigit       = true;  // Track first digit for leading zero check (RFC 8259)
        bool leadingZero      = false; // Track if first digit was '0' (for RFC 8259 leading zero check)

        if (atEnd())   {
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            return false;
        }

        // Optional leading '-'
        {
            char c = *current_;
            if (c == '-') {
                if (index >= fp_to_str_detail::NumberBufSize - 1) {
                    setError(JsonIteratorReaderError::ILLFORMED_NUMBER);
                    return false;
                }
                buf[index++] = c;
                ++current_;
            }
        }

        if (atEnd())   {
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            return false;
        }

        auto push_char = [&](char c) -> bool {
            if (index >= fp_to_str_detail::NumberBufSize - 1) {
                setError(JsonIteratorReaderError::ILLFORMED_NUMBER);
                return false;
            }
            buf[index++] = c;
            return true;
        };

        while (true) {
            if(atEnd()) {
                break;
            }
            if(isPlainEnd(*current_)){
                break;
            }
            char c = *current_;

            if (c >= '0' && c <= '9') {
                // RFC 8259: Leading zeros not allowed (except "0" itself, "0.x", "0eX")
                // If first digit was '0' and we see another digit without dot/exp, it's invalid.
                // This check uses state instead of peek-ahead to support single-pass iterators.
                if (leadingZero && !seenDot && !inExp) {
                    setError(JsonIteratorReaderError::ILLFORMED_NUMBER);
                    return false;
                }

                if (firstDigit && c == '0') {
                    leadingZero = true;
                }
                firstDigit = false;  // Mark that we've seen the first digit

                if (!inExp) {
                    seenDigitBeforeExp = true;
                } else {
                    seenDigitAfterExp = true;
                }
                if (!push_char(c)) return false;
                ++current_;
                continue;
            }

            if (c == '.' && !seenDot && !inExp) {
                // RFC 8259: Decimal point requires digits before AND after
                if (!seenDigitBeforeExp) {
                    // Leading dot like ".42" is invalid
                    setError(JsonIteratorReaderError::ILLFORMED_NUMBER);
                    return false;
                }
                
                seenDot = true;
                if (!push_char(c)) return false;
                ++current_;
                
                // Ensure at least one digit follows the decimal point
                if (atEnd() || !(*current_ >= '0' && *current_ <= '9')) {
                    // Trailing dot like "42." is invalid
                    setError(JsonIteratorReaderError::ILLFORMED_NUMBER);
                    return false;
                }
                continue;
            }

            if ((c == 'e' || c == 'E') && !inExp) {
                inExp  = true;
                seenExp = true;
                if (!push_char(c)) return false;
                ++current_;

                // Optional sign immediately after exponent
                if (current_ != end_ && (*current_ == '+' || *current_ == '-')) {
                    if (!push_char(*current_)) return false;
                    ++current_;
                }
                continue;
            }

            // '+' or '-' is only allowed immediately after 'e'/'E', which we handled above
            // Anything else is invalid inside a JSON number
            setError(JsonIteratorReaderError::ILLFORMED_NUMBER);
            return false;
        }

        buf[index] = '\0';

        // Basic sanity: must have at least one digit before exponent,
        // and if exponent present, at least one digit after it.
        if (!seenDigitBeforeExp || (seenExp && !seenDigitAfterExp && inExp)) {
            setError(JsonIteratorReaderError::ILLFORMED_NUMBER);
            return false;
        }

        return true;
    }
    template <std::size_t MAX_SKIP_NESTING, class Filler>
    constexpr bool skip_json_value_internal(Filler &filler, std::size_t MaxStringLength = std::numeric_limits<std::size_t>::max()) {
        if (!skip_whitespace())  {
            return false;
        }


        // Helper: skip a JSON literal (true/false/null), mirroring chars to sink.
        auto skipLiteral = [&] (const char* lit,
                               std::size_t len,
                               JsonIteratorReaderError err) -> bool
        {
            for (std::size_t i = 0; i < len; ++i) {
                if (atEnd())  {
                    setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                    return false;
                }

                if (*current_ != lit[i]) {
                    setError(err);
                    return false;
                }
                if (!filler(*current_)) {
                    setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                    return false;
                }
                ++current_;
            }
            return true;
        };

        // Helper: skip a number-like token, mirroring chars to sink.
        auto skipNumberLike = [&]() -> bool {
            while (!isPlainEnd(*current_)) {
                if(atEnd())  {
                    break;
                }

                if (!filler(*current_)) {
                    setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                    return false;
                }
                ++current_;
            }
            return true;
        };

        char c = *current_;

        // 1) Simple values we can skip without nesting

        if (c == '"') {

            return skip_string_raw_via_filler(filler);
        }

        if (c == 't') {
            // true
            return skipLiteral("true", 4, JsonIteratorReaderError::ILLFORMED_BOOL);
        }
        if (c == 'f') {
            // false
            return skipLiteral("false", 5, JsonIteratorReaderError::ILLFORMED_BOOL);
        }
        if (c == 'n') {
            // null
            return skipLiteral("null", 4, JsonIteratorReaderError::ILLFORMED_NULL);
        }

        // number-like: skip until a plain end (whitespace, ',', ']', '}', etc.)
        if (c != '{' && c != '[') {
            // neither object nor array → treat as number-like token
            return skipNumberLike();
        }

        // 2) Compound value: object or array with possible nesting.
        // We use an explicit stack of expected closing delimiters to avoid recursion.

        char stack[MAX_SKIP_NESTING];
        int  depth = 0;

        auto push_close = [&](char open) -> bool {
            if (depth >= static_cast<int>(MAX_SKIP_NESTING)) {
                setError(JsonIteratorReaderError::SKIPPING_STACK_OVERFLOW);
                return false;
            }
            stack[depth++] = (open == '{') ? '}' : ']';
            return true;
        };

        auto pop_close = [&](char close) -> bool {
            if (depth == 0)  {
                setError(JsonIteratorReaderError::ILLFORMED_OBJECT);
                return false;
            }
            if (stack[depth - 1] != close) {
                setError(JsonIteratorReaderError::ILLFORMED_OBJECT);
                return false;
            }
            --depth;
            return true;
        };

        // Initialize with the first '{' or '['
        if (!push_close(c))  {
            return false;
        }
        // mirror the opening delimiter
        if (!filler(c)) {
            setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
            return false;
        }
        ++current_; // skip first '{' or '['

        while (current_ != end_ && depth > 0) {
            char ch = *current_;

            // Skip whitespace cheaply; you can decide whether to mirror it or not.
            if (isSpace(ch)) {
                ++current_;
                continue;
            }

            switch (ch) {
            case '"': {
                // mirrors the entire string via sinkInserter
                if (!skip_string_raw_via_filler(filler) ) {
                    return false;
                }
                break;
            }
            case '{':
            case '[': {
                if (!push_close(ch)) {
                    return false;
                }
                if (!filler(ch))  {
                    setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                    return false;
                }
                ++current_;
                break;
            }
            case '}':
            case ']': {
                if (!pop_close(ch)) {
                    return false;
                }
                if(!filler(ch)) {
                    setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                    return false;
                }
                ++current_;
                break;
            }
            case 't': {
                if (!skipLiteral("true", 4, JsonIteratorReaderError::ILLFORMED_BOOL)) {
                    return false;
                }
                break;
            }
            case 'f': {
                if (!skipLiteral("false", 5, JsonIteratorReaderError::ILLFORMED_BOOL))  {
                    return false;
                }
                break;
            }
            case 'n': {
                if (!skipLiteral("null", 4, JsonIteratorReaderError::ILLFORMED_NULL))  {
                    return false;
                }
                break;
            }
            default: {
                // number-like or punctuation (':', ',', etc.)
                if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+') {
                    if (!skipNumberLike()) {
                        return false;
                    }
                } else {
                    // punctuation: mirror and advance
                    if (!filler(ch))  {
                        setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                        return false;
                    }
                    ++current_;
                }
                break;
            }
            }
        }

        if (depth != 0)  {
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            return false;
        }

        filler.finish();

        return true;
    }

    JsonIteratorReaderError m_error;
    It current_;
    It m_errorPos;
    Sent end_;



    char   string_buf_[4]   = {}; // temp buffer for escapes / UTF-8
    std::size_t    string_buf_len_  = 0;
    std::size_t    string_buf_pos_  = 0;
    bool   in_string_       = false;
    std::uint32_t  utf8_state_ = utf8_detail::UTF8_ACCEPT; // incremental raw-UTF-8 validation state

    constexpr void setError(JsonIteratorReaderError e) {
        m_error = e;
        m_errorPos = current_;
    }



    constexpr bool atEnd() {
        return current_ == end_;
    }


    constexpr bool isPlainEnd(char a) {
        switch(a) {
        case ']':
        case ',':
        case '}':
        case 0x20:
        case 0x0A:
        case 0x0D:
        case 0x09:
            return true;
        }
        return false;
    }

    constexpr bool isSpace(char c) noexcept {
        // ASCII JSON whitespace: space, LF, CR, TAB
        return c == ' ' || c == '\n' || c == '\r' || c == '\t';
    }

    constexpr bool skip_whitespace() {

        // Fast path: just chew through spaces until non-space or end
        while (current_ != end_ && isSpace(*current_)) {

            ++current_;
        }
        return true;
    }

    constexpr  bool match_literal(const std::string_view & lit) {
        for (char c : lit) {
            if (atEnd())  {
                setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                return false;
            }
            if (*current_ != c)  {
                return false;
            }
            ++current_;
        }
        return true;
    }

    // -------------------------
    //  Parse decimal integer
    // -------------------------
    // buf: null-terminated ASCII, optional leading '+'/'-' then digits.
    // Assumes characters are already known to be digits (no extra validation).
    // Returns false on overflow or invalid '-' for unsigned types.
    template <class Int>
    constexpr bool parse_decimal_integer(const char* buf, Int& out) noexcept {
        static_assert(std::is_integral_v<Int>, "[[[ JsonFusion ]]] Int must be an integral type");

        using Limits   = std::numeric_limits<Int>;
        using Unsigned = std::make_unsigned_t<Int>;

        // const unsigned char* p = reinterpret_cast<const unsigned char*>(buf);
        const char *p = buf;
        bool negative = false;

        if constexpr (std::is_signed_v<Int>) {
            if (*p == '+' || *p == '-') {
                negative = (*p == '-');
                ++p;
            }
        } else {
            // Unsigned: allow leading '+' but reject '-'
            if (*p == '+') {
                ++p;
            } else if (*p == '-') {
                return false; // negative value for unsigned -> overflow/error
            }
        }

        Unsigned value = 0;

        // Absolute-value limit we must not exceed while parsing
        Unsigned limit;
        if constexpr (std::is_signed_v<Int>) {
            // For negative numbers we allow up to |min()| = max()+1
            limit = negative ? Unsigned(Limits::max()) + 1u
                             : Unsigned(Limits::max());
        } else {
            limit = std::numeric_limits<Unsigned>::max();
        }

        for (; *p != 0; ++p) {
            // We assume *p is '0'..'9'
            unsigned digit = static_cast<unsigned>(*p - '0');

            // Check overflow: value*10 + digit <= limit
            if (value > (limit - digit) / 10u)  {
                return false; // overflow
            }
            value = value * 10u + static_cast<Unsigned>(digit);
        }

        if constexpr (std::is_signed_v<Int>) {
            if (negative) {
                // Special-case: most-negative value
                if (value == Unsigned(Limits::max()) + 1u) {
                    out = Limits::min();
                } else {
                    out = static_cast<Int>(-static_cast<Int>(value));
                }
            } else {
                out = static_cast<Int>(value);
            }
        } else {
            out = static_cast<Int>(value);
        }

        return true;
    }

    constexpr  bool readHex4(std::uint16_t &out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            if (atEnd()) {
                setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                return false;
            }
            char currChar = *current_;
            std::uint8_t v;
            if (currChar >= '0' && currChar <= '9') {
                v = static_cast<std::uint8_t>(currChar - '0');
            } else if (currChar >= 'A' && currChar <= 'F') {
                v = static_cast<std::uint8_t>(currChar - 'A' + 10);
            } else if (currChar >= 'a' && currChar <= 'f') {
                v = static_cast<std::uint8_t>(currChar - 'a' + 10);
            } else  {
                setError(JsonIteratorReaderError::ILLFORMED_STRING);
                return false;
            }
            out = static_cast<std::uint16_t>((out << 4) | v);
            ++current_;
        }
        return true;
    }

    template<class Cont>
    struct DynContainerFiller {
        Cont * out;
        std::size_t max_size = 0;
        std::size_t inserted = 0;

        constexpr bool operator()(char ch) {
            if (inserted >= max_size) {
                return false;
            }
            out->push_back(ch);
            inserted++;
            return true;
        }
        constexpr void finish(){

        }
    };

    struct StContainerFiller {
        char * out = nullptr;
        std::size_t max_size = 0;
        std::size_t inserted = 0;
        constexpr bool operator()(char ch) {
            if (inserted >= max_size - 1) {
                return false;
            }
            out[inserted] = ch;
            inserted ++;
            return true;
        }
        constexpr void finish(){
            out[inserted] = 0;
        }
    };
    struct NoOpFiller {
        void * out = nullptr;
        std::size_t max_size = std::numeric_limits<std::size_t>::max();
        std::size_t inserted = 0;
        constexpr bool operator()(char ch) {
            inserted ++;
            return true;
        }
        constexpr void finish(){}
    };

    template<WireSinkLike Sink>
    struct WireSinkFiller {
        Sink* sink;
        bool overflow = false;
        
        constexpr bool operator()(char ch) {
            if (overflow) return false;
            
            if (!sink->write(&ch, 1)) {
                overflow = true;
                return false;
            }
            return true;
        }
        
        constexpr void finish() {
            // WireSink already tracks size internally
        }
    };

    template<class Filler>
    constexpr bool skip_string_raw_via_filler(Filler& f) {
        // Copy raw JSON string (with escapes) char-by-char through filler
        // Validates JSON string syntax but doesn't unescape
        
        // Expect opening quote
        if (atEnd() || *current_ != '"') {
            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
            return false;
        }
        if (!f('"')) {
            setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
            return false;
        }
        ++current_;

        // Incremental UTF-8 validation of raw string bytes (skipped values / keys).
        std::uint32_t st = utf8_detail::UTF8_ACCEPT;

        // Read until closing quote
        while (true) {
            if (atEnd()) {
                setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                return false;
            }
            
            char c = *current_;
            
            // Closing quote
            if (c == '"') {
                if (st != utf8_detail::UTF8_ACCEPT) {
                    setError(JsonIteratorReaderError::INVALID_UTF8);
                    return false;
                }
                if (!f('"')) {
                    setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                    return false;
                }
                ++current_;
                return true;
            }
            
            // Escape sequence
            if (c == '\\') {
                if (st != utf8_detail::UTF8_ACCEPT) {
                    setError(JsonIteratorReaderError::INVALID_UTF8);
                    return false;
                }
                if (!f('\\')) {
                    setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                    return false;
                }
                ++current_;
                
                if (atEnd()) {
                    setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                    return false;
                }
                
                char esc = *current_;
                
                // Validate and copy escape character
                switch (esc) {
                case '"':
                case '/':
                case '\\':
                case 'b':
                case 'f':
                case 'r':
                case 'n':
                case 't':
                    if (!f(esc)) {
                        setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                        return false;
                    }
                    ++current_;
                    break;
                    
                case 'u': {
                    // Unicode escape: \uXXXX (and potentially surrogate pair)
                    if (!f('u')) {
                        setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                        return false;
                    }
                    ++current_;
                    
                    // Read and validate 4 hex digits
                    char hex[4];
                    for (int i = 0; i < 4; ++i) {
                        if (atEnd()) {
                            setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                            return false;
                        }
                        char h = *current_;
                        if (!((h >= '0' && h <= '9') || (h >= 'A' && h <= 'F') || (h >= 'a' && h <= 'f'))) {
                            setError(JsonIteratorReaderError::ILLFORMED_STRING);
                            return false;
                        }
                        hex[i] = h;
                        if (!f(h)) {
                            setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                            return false;
                        }
                        ++current_;
                    }
                    
                    // Check if it's a high surrogate (D800-DBFF), which requires a pair
                    std::uint16_t u1 = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = hex[i];
                        std::uint8_t v;
                        if (h >= '0' && h <= '9') {
                            v = static_cast<std::uint8_t>(h - '0');
                        } else if (h >= 'A' && h <= 'F') {
                            v = static_cast<std::uint8_t>(h - 'A' + 10);
                        } else {
                            v = static_cast<std::uint8_t>(h - 'a' + 10);
                        }
                        u1 = static_cast<std::uint16_t>((u1 << 4) | v);
                    }
                    
                    // High surrogate: expect \uXXXX for low surrogate
                    if (u1 >= 0xD800u && u1 <= 0xDBFFu) {
                        if (atEnd() || *current_ != '\\') {
                            setError(JsonIteratorReaderError::ILLFORMED_STRING);
                            return false;
                        }
                        if (!f('\\')) {
                            setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                            return false;
                        }
                        ++current_;
                        
                        if (atEnd() || *current_ != 'u') {
                            setError(JsonIteratorReaderError::ILLFORMED_STRING);
                            return false;
                        }
                        if (!f('u')) {
                            setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                            return false;
                        }
                        ++current_;
                        
                        // Read and validate second 4 hex digits
                        std::uint16_t u2 = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (atEnd()) {
                                setError(JsonIteratorReaderError::UNEXPECTED_END_OF_DATA);
                                return false;
                            }
                            char h = *current_;
                            if (!((h >= '0' && h <= '9') || (h >= 'A' && h <= 'F') || (h >= 'a' && h <= 'f'))) {
                                setError(JsonIteratorReaderError::ILLFORMED_STRING);
                                return false;
                            }
                            std::uint8_t v;
                            if (h >= '0' && h <= '9') {
                                v = static_cast<std::uint8_t>(h - '0');
                            } else if (h >= 'A' && h <= 'F') {
                                v = static_cast<std::uint8_t>(h - 'A' + 10);
                            } else {
                                v = static_cast<std::uint8_t>(h - 'a' + 10);
                            }
                            u2 = static_cast<std::uint16_t>((u2 << 4) | v);
                            
                            if (!f(h)) {
                                setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                                return false;
                            }
                            ++current_;
                        }
                        
                        // Validate low surrogate range
                        if (u2 < 0xDC00u || u2 > 0xDFFFu) {
                            setError(JsonIteratorReaderError::ILLFORMED_STRING);
                            return false;
                        }
                    } else if (u1 >= 0xDC00u && u1 <= 0xDFFFu) {
                        // Lone low surrogate
                        setError(JsonIteratorReaderError::ILLFORMED_STRING);
                        return false;
                    }
                    break;
                }
                    
                default:
                    setError(JsonIteratorReaderError::ILLFORMED_STRING);
                    return false;
                }
                continue;
            }
            
            // Control characters must be escaped (RFC 8259 §7)
            if (static_cast<unsigned char>(c) <= 0x1F) {
                setError(JsonIteratorReaderError::ILLFORMED_STRING);
                return false;
            }
            
            // Normal character - validate raw UTF-8, then copy
            st = utf8_detail::utf8_step(st, static_cast<unsigned char>(c));
            if (st == utf8_detail::UTF8_REJECT) {
                setError(JsonIteratorReaderError::INVALID_UTF8);
                return false;
            }
            if (!f(c)) {
                setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                return false;
            }
            ++current_;
        }
    }

    template<class Filler>
    constexpr bool read_string_with_filler(Filler & f) {
        std::size_t added = 0;
        f('"');
        bool r = read_json_string_into(f.out, f.inserted, f.max_size - f.inserted, added);
        if(r) {
            f.inserted += added;
            f('"');
        }
        return r;
    }
    static constexpr std::size_t STRING_CHUNK_SIZE = 64;

    template<class Cont>
    constexpr bool read_json_string_into(
        Cont * out,
        std::size_t initS,
        std::size_t  max_len,  // validator limit or SIZE_MAX
        std::size_t & outSize
        )
    {
        std::size_t total = 0;
        bool done = false;

        // Optional: pre-reserve up to max_len to reduce reallocations
        if constexpr(!std::is_same_v<Cont, void> && !std::is_same_v<Cont, char>) {
            if (max_len == std::numeric_limits<std::size_t>::max()) {
                out->reserve(STRING_CHUNK_SIZE);

            }
        }

        while (!done) {
            std::size_t remaining = max_len - total;
            if (remaining == 0) {
                // Overflow: we hit the max_len but reader still has string data
                setError(JsonIteratorReaderError::WIRE_SINK_OVERFLOW);
                return false;
            }

            constexpr std::size_t CHUNK = STRING_CHUNK_SIZE;
            std::size_t ask = remaining < CHUNK ? remaining : CHUNK;

            // Make sure string has space [total, total+ask) to write into.
            // This also keeps size() in a valid state.
            reader::StringChunkResult res;
            if constexpr(!std::is_same_v<Cont, void> && !std::is_same_v<Cont, char>) {
                out->resize(initS + total + ask);
                res = read_string_chunk(out->data() + initS + total, ask);
            } else if constexpr (std::is_same_v<Cont, char>) {
                res = read_string_chunk(out + initS + total,
                                        max_len - total);
            } else {
                char b[CHUNK];
                res = read_string_chunk(b, ask);
            }

            if(res.status != reader::StringChunkStatus::ok) {
                return false;
            }

            total += res.bytes_written;

            if (res.done) {
                // Shrink to actual number of bytes written
                outSize = total;
                if constexpr(!std::is_same_v<Cont, void> && !std::is_same_v<Cont, char>) {
                    out->resize(initS + total);
                }
                return true;
            }

            // Not done yet; if reader filled less than asked but not done,
            // you might optionally treat that as suspicious, but generally fine.
            // Loop continues.
        }

        // Shouldn’t be reachable
        setError(JsonIteratorReaderError::ILLFORMED_STRING);
        return false;
    }


};
static_assert(JsonFusion::reader::ReaderLike<JsonIteratorReader<char*, char*, 64>>);

enum class JsonIteratorWriterError {
    NO_ERROR,
    OUTPUT_OVERFLOW,
    INVALID_UTF8
};

template<class It, class Sent, bool Pretty = false>
class JsonIteratorWriter {
public:
    using iterator_type = It;
    struct ArrayFrame {
        std::size_t depth = 0;  // Track depth for indentation
    };
    struct MapFrame {
        std::size_t depth = 0;  // Track depth for indentation
    };

    using error_type = JsonIteratorWriterError;

    JsonIteratorWriterError m_error;
    constexpr JsonIteratorWriterError getError() {
        return m_error;
    }

    constexpr void setError(JsonIteratorWriterError e) {
        m_error = e;
        m_errorPos = m_current;
    }
    It m_errorPos;
    It m_current;
    std::size_t m_bytesWritten = 0;
    std::uint32_t utf8_state_ = utf8_detail::UTF8_ACCEPT; // incremental UTF-8 validation of emitted string bytes
    Sent end_;
    constexpr It current() {
        return m_current;
    }
    std::size_t m_float_decimals=8;
    std::size_t m_indent_level = 0;  // Current indentation level
    std::size_t m_indent_size = 2;   // Spaces per indent level
    
    constexpr JsonIteratorWriter(It first, Sent last, std::size_t float_decimals=8, std::size_t indent_size=2)
        : m_error(JsonIteratorWriterError::NO_ERROR), m_errorPos(first), m_current(first), end_(last), m_float_decimals(float_decimals), m_indent_size(indent_size) {}

private:
    // Helper to write newline + indentation
    constexpr bool write_indent() {
        if constexpr (Pretty) {
            // Write newline
            if (m_current == end_) {
                setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                return false;
            }
            *m_current++ = '\n';
            m_bytesWritten ++;
            
            // Write spaces for indentation
            for (std::size_t i = 0; i < m_indent_level * m_indent_size; ++i) {
                if (m_current == end_) {
                    m_bytesWritten += i;
                    setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                    return false;
                }
                *m_current++ = ' ';

            }
            m_bytesWritten += m_indent_level * m_indent_size;
        }
        return true;
    }

public:

    __attribute__((noinline)) constexpr bool write_array_begin(const std::size_t &, ArrayFrame& frame) {
        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current ++ = '['; m_bytesWritten ++;
        
        if constexpr (Pretty) {
            frame.depth = m_indent_level;
            m_indent_level++;
            if (!write_indent()) return false;
        }
        return true;
    }
    __attribute__((noinline)) constexpr bool write_map_begin(const std::size_t &, MapFrame& frame) {
        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current++ = '{'; m_bytesWritten ++;
        
        if constexpr (Pretty) {
            frame.depth = m_indent_level;
            m_indent_level++;
            if (!write_indent()) return false;
        }
        return true;
    }

    __attribute__((noinline))constexpr bool advance_after_value(ArrayFrame&) {
        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current ++ = ','; m_bytesWritten ++;
        
        if constexpr (Pretty) {
            if (!write_indent()) return false;
        }
        return true;
    }
    __attribute__((noinline)) constexpr bool advance_after_value(MapFrame&) {
        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current ++ = ','; m_bytesWritten ++;
        
        if constexpr (Pretty) {
            if (!write_indent()) return false;
        }
        return true;
    }
    __attribute__((noinline)) constexpr bool move_to_value(MapFrame&) {

        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current++ = ':'; m_bytesWritten ++;
        
        if constexpr (Pretty) {
            // Add space after colon for readability
            if(m_current == end_) {
                setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                return false;
            }
            *m_current++ = ' '; m_bytesWritten ++;
        }

        return true;
    }

    __attribute__((noinline)) constexpr bool write_key_as_index(const std::int64_t &int_key) {
        char buf[fp_to_str_detail::NumberBufSize];

        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current ++ = '"'; m_bytesWritten ++;


        char* p = format_decimal_integer<std::int64_t>(int_key, buf, buf + sizeof(buf));
        for (char* it = buf; it != p; ++it) {
            if (m_current == end_) {
                setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                return false;
            }
            *m_current++ = *it; m_bytesWritten ++;
        }

        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current ++ = '"'; m_bytesWritten ++;

        return true;
    }

    __attribute__((noinline)) constexpr bool write_array_end(ArrayFrame& frame) {
        if constexpr (Pretty) {
            m_indent_level = frame.depth;
            if (!write_indent()) return false;
        }
        
        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current ++ = ']'; m_bytesWritten ++;
        return true;
    }
    __attribute__((noinline)) constexpr bool write_map_end(MapFrame& frame) {
        if constexpr (Pretty) {
            m_indent_level = frame.depth;
            if (!write_indent()) return false;
        }
        
        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current++ = '}'; m_bytesWritten ++;
        return true;
    }

    constexpr bool write_null() {
        return serialize_literal("null");
    }
    constexpr bool write_bool(const bool & obj) {
        if(obj) {
            return serialize_literal("true");
        } else {
            return serialize_literal("false");
        }
    }

    template<class NumberT>
    __attribute__((noinline)) constexpr bool write_number(const NumberT & v){
        char buf[fp_to_str_detail::NumberBufSize];
        if constexpr (std::is_integral_v<NumberT>) {
            char* p = format_decimal_integer<NumberT>(v, buf, buf + sizeof(buf));
            for (char* it = buf; it != p; ++it) {
                if (m_current == end_) {
                    setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                    return false;
                }
                *m_current++ = *it; m_bytesWritten ++;
            }
            return true;
        } else {

            double content = v;

            if(std::isnan(content) || std::isinf(content)) {
                if(!serialize_literal("0")) {
                    setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                    return false;
                }
                return true;
            } else {

                char * endChar = fp_to_str_detail::format_double_to_chars(buf, content, m_float_decimals);
                auto s = endChar-buf;
                if(endChar-buf == sizeof (buf)) {
                    return false;
                }

                for(int i = 0; i < s; i ++) {
                    if(m_current == end_) {
                        setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                        return false;
                    }
                    *m_current ++ = buf[i]; m_bytesWritten ++;
                }
                return true;

            }
        }
    }
    // Chunked string writing - JSON ignores size_hint (delimiter-based format)
    __attribute__((noinline)) constexpr bool write_string_begin(std::size_t /*size_hint*/) {
        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        utf8_state_ = utf8_detail::UTF8_ACCEPT;
        *m_current++ = '"'; m_bytesWritten++;
        return true;
    }
    
    __attribute__((noinline))
    constexpr bool write_string_chunk(const char* data, std::size_t size) {
        constexpr char hex[] = "0123456789abcdef";

        // Helper for generic (non-pointer) output
        auto put_generic = [&](char ch) constexpr -> bool {
            if (m_current == end_) {
                setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                return false;
            }
            *m_current = ch;
            ++m_current;
            ++m_bytesWritten;
            return true;
        };

        const char* p = data;
        const char* e = data + size;

        if constexpr (std::is_pointer_v<decltype(m_current)> && std::is_pointer_v<decltype(end_)>) {
            // Fast path: contiguous output pointers (still constexpr-friendly: no memcpy)
            auto* cur = m_current;
            auto* end = end_;
            std::size_t bw = m_bytesWritten;

            auto ensure = [&](std::size_t n) constexpr -> bool {
                if (static_cast<std::size_t>(end - cur) < n) {
                    setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                    return false;
                }
                return true;
            };

            auto put2 = [&](char a, char b) constexpr -> bool {
                if (!ensure(2)) return false;
                *cur++ = a;
                *cur++ = b;
                bw += 2;
                return true;
            };

            auto put6_u00 = [&](unsigned char uc) constexpr -> bool {
                if (!ensure(6)) return false;
                *cur++ = '\\';
                *cur++ = 'u';
                *cur++ = '0';
                *cur++ = '0';
                *cur++ = hex[(uc >> 4) & 0xF];
                *cur++ = hex[uc & 0xF];
                bw += 6;
                return true;
            };

            while (p < e) {
                // Find next byte that needs escaping: '"' or '\\' or control (<0x20)
                const char* run = p;
                while (run < e) {
                    unsigned char uc = static_cast<unsigned char>(*run);
                    if (*run == '"' || *run == '\\' || uc < 0x20) break;
                    utf8_state_ = utf8_detail::utf8_step(utf8_state_, uc);
                    if (utf8_state_ == utf8_detail::UTF8_REJECT) {
                        setError(JsonIteratorWriterError::INVALID_UTF8);
                        return false;
                    }
                    ++run;
                }

                // Bulk-copy the safe run (constexpr-friendly loop)
                if (run != p) {
                    const std::size_t n = static_cast<std::size_t>(run - p);
                    if (!ensure(n)) return false;
                    for (std::size_t i = 0; i < n; ++i) {
                        cur[i] = p[i];
                    }
                    cur += n;
                    bw += n;
                    p = run;
                    continue;
                }

                // Slow path: one byte requiring escaping. A special (ASCII) byte
                // interrupting a pending multibyte sequence makes it truncated/invalid.
                unsigned char uc = static_cast<unsigned char>(*p++);
                utf8_state_ = utf8_detail::utf8_step(utf8_state_, uc);
                if (utf8_state_ == utf8_detail::UTF8_REJECT) {
                    setError(JsonIteratorWriterError::INVALID_UTF8);
                    return false;
                }
                switch (uc) {
                case '"':  if (!put2('\\', '"'))  return false; break;
                case '\\': if (!put2('\\', '\\')) return false; break;
                case '\b': if (!put2('\\', 'b'))  return false; break;
                case '\f': if (!put2('\\', 'f'))  return false; break;
                case '\n': if (!put2('\\', 'n'))  return false; break;
                case '\r': if (!put2('\\', 'r'))  return false; break;
                case '\t': if (!put2('\\', 't'))  return false; break;
                default:
                    if (uc < 0x20) {
                        if (!put6_u00(uc)) return false;
                    } else {
                        // Shouldn't happen (handled by run scan), but keep correctness.
                        if (!ensure(1)) return false;
                        *cur++ = static_cast<char>(uc);
                        bw += 1;
                    }
                    break;
                }
            }

            // Commit locals back to members
            m_current = cur;
            m_bytesWritten = bw;
            return true;
        } else {
            // Generic path: works for any output iterator (byte-by-byte)
            while (p < e) {
                // Safe run (still scanning, but emitting is per-byte)
                const char* run = p;
                while (run < e) {
                    unsigned char uc = static_cast<unsigned char>(*run);
                    if (*run == '"' || *run == '\\' || uc < 0x20) break;
                    utf8_state_ = utf8_detail::utf8_step(utf8_state_, uc);
                    if (utf8_state_ == utf8_detail::UTF8_REJECT) {
                        setError(JsonIteratorWriterError::INVALID_UTF8);
                        return false;
                    }
                    ++run;
                }

                while (p < run) {
                    if (!put_generic(*p++)) return false;
                }

                if (p == e) break;

                unsigned char uc = static_cast<unsigned char>(*p++);
                utf8_state_ = utf8_detail::utf8_step(utf8_state_, uc);
                if (utf8_state_ == utf8_detail::UTF8_REJECT) {
                    setError(JsonIteratorWriterError::INVALID_UTF8);
                    return false;
                }
                switch (uc) {
                case '"':  if (!put_generic('\\') || !put_generic('"'))  return false; break;
                case '\\': if (!put_generic('\\') || !put_generic('\\')) return false; break;
                case '\b': if (!put_generic('\\') || !put_generic('b'))  return false; break;
                case '\f': if (!put_generic('\\') || !put_generic('f'))  return false; break;
                case '\n': if (!put_generic('\\') || !put_generic('n'))  return false; break;
                case '\r': if (!put_generic('\\') || !put_generic('r'))  return false; break;
                case '\t': if (!put_generic('\\') || !put_generic('t'))  return false; break;
                default:
                    if (uc < 0x20) {
                        if (!put_generic('\\') || !put_generic('u') || !put_generic('0') || !put_generic('0')) return false;
                        if (!put_generic(hex[(uc >> 4) & 0xF]) || !put_generic(hex[uc & 0xF])) return false;
                    } else {
                        if (!put_generic(static_cast<char>(uc))) return false;
                    }
                    break;
                }
            }
            return true;
        }
    }

    
    __attribute__((noinline)) constexpr bool write_string_end() {
        // A string that ends mid-sequence held a truncated multibyte code point.
        if (utf8_state_ != utf8_detail::UTF8_ACCEPT) {
            setError(JsonIteratorWriterError::INVALID_UTF8);
            return false;
        }
        if(m_current == end_) {
            setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
            return false;
        }
        *m_current++ = '"'; m_bytesWritten++;
        return true;
    }
    
    // Convenience wrapper for single-call string writing (maintains old behavior)
    __attribute__((noinline)) constexpr bool write_string(const char* data, std::size_t size, bool null_ended = false) {
        if (null_ended) {
            // Find null terminator
            std::size_t actual_size = 0;
            while (actual_size < size && data[actual_size] != '\0') {
                actual_size++;
            }
            size = actual_size;
        }
        if (!write_string_begin(size)) return false;
        if (!write_string_chunk(data, size)) return false;
        return write_string_end();
    }

    constexpr std::size_t finish() {
        if(m_current == end_) return -1;
        return m_bytesWritten;
    }
    
    // WireSink support - output raw data from sink to JSON stream
    template<WireSinkLike Sink>
    constexpr bool output_from_sink(const Sink& sink) {
        const std::size_t size = sink.current_size();
        const char* data = sink.data();
        
        for (std::size_t i = 0; i < size; ++i) {
            if (m_current == end_) {
                setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                return false;
            }
            *m_current++ = data[i]; m_bytesWritten ++;
        }
        
        return true;
    }

    static constexpr auto from_sink(WireSinkLike auto & sink) {
        return JsonIteratorWriter<char*, char*, Pretty>(sink.data(), sink.data() + sink.max_size());
    }
    constexpr bool serialize_literal(std::string_view lit) {
        for (char c : lit) {
            if (m_current == end_) {
                setError(JsonIteratorWriterError::OUTPUT_OVERFLOW);
                return false;
            }
            *m_current++ = c; m_bytesWritten ++;
        }
        return true;
    }


    // -------------------------
    //  Format decimal integer
    // -------------------------
    // Writes base-10 representation of value into [first, last).
    // Returns pointer one past last written char.
    // Caller guarantees buffer is large enough (e.g. NumberBufSize).
    template <class Int>
    constexpr char* format_decimal_integer(Int value,
                                           char* first,
                                           char* last) noexcept {
        static_assert(std::is_integral_v<Int>, "[[[ JsonFusion ]]] Int must be an integral type");

        // We'll generate digits into the end of the buffer, then memmove forward.
        char* p = last;

        using Unsigned = std::make_unsigned_t<Int>;
        Unsigned u;
        bool negative = false;

        if constexpr (std::is_signed_v<Int>) {
            if (value < 0) {
                negative = true;
                // Compute absolute value safely:
                // for min() this avoids UB by doing it in unsigned domain.
                u = Unsigned(-(value + 1)) + 1u;
            } else {
                u = static_cast<Unsigned>(value);
            }
        } else {
            u = static_cast<Unsigned>(value);
        }

        // Generate digits in reverse order
        do {
            if (p == first) {
                // Not enough space; best-effort: just stop
                break;
            }
            unsigned digit = static_cast<unsigned>(u % 10u);
            u /= 10u;
            *--p = static_cast<char>('0' + digit);
        } while (u != 0);

        if (negative) {
            if (p != first) {
                *--p = '-';
            } else {
                // No room for '-', we just overwrite first char
                *p = '-';
            }
        }

        // Now we have the result in [p, last). Move it to start at 'first'.
        std::size_t len = static_cast<std::size_t>(last - p);
        // Assume caller provided a big enough buffer (e.g. NumberBufSize),
        // but clamp just in case.
        if (static_cast<std::size_t>(last - first) < len) {
            len = static_cast<std::size_t>(last - first);
        }
        for (std::size_t i = 0; i < len; ++i)
            first[i] = p[i];
        // std::memmove(first, p, len);
        return first + len;
    }
};

static_assert(JsonFusion::writer::WriterLike<JsonIteratorWriter<char*, char*, false>>);
static_assert(JsonFusion::writer::WriterLike<JsonIteratorWriter<char*, char*, true>>);

} // namespace JsonFusion
