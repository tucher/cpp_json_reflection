#pragma once

// Optional high-performance backend (yyjson). Guarded so this header is always
// safe to include: it compiles to nothing unless <yyjson.h> is on the include path.
#if __has_include(<yyjson.h>)
#include <yyjson.h>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <charconv>
#include <functional>
#include "reader_concept.hpp"
#include "writer_concept.hpp"

namespace JsonFusion {

class YyjsonReader {
public:
    enum class ParseError {
        NO_ERROR,
        UNEXPECTED_END_OF_DATA,
        ILLFORMED_OBJECT,
        ILLFORMED_ARRAY,
        NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE
    };
    using error_type = ParseError;

    using iterator_type = yyjson_val*; // to satisfy Parser's current() API

    // Per-container state lives here, not in the reader.
    struct ArrayFrame {
        yyjson_val*   arr      = nullptr;
        yyjson_arr_iter it{};
        std::size_t   index    = 0;     // current element index
        std::size_t   size     = 0;     // total elements
        yyjson_val*   current  = nullptr; // current element, or nullptr if empty/done
    };

    struct MapFrame {
        yyjson_val*   obj      = nullptr;
        yyjson_obj_iter it{};
        yyjson_val*   key      = nullptr; // current key node
        yyjson_val*   value    = nullptr; // current value node
    };

    // Constructor for external document (user manages lifetime)
    explicit YyjsonReader(yyjson_val* root) noexcept
        : root_(root)
        , current_(root)
        , doc_(nullptr)
        , owns_doc_(false)
        , err_(ParseError::NO_ERROR)
    {}
    
    // Constructor that parses JSON and owns document (RAII)
    explicit YyjsonReader(const char* json_data, std::size_t json_len) noexcept
        : root_(nullptr)
        , current_(nullptr)
        , doc_(nullptr)
        , owns_doc_(true)
        , err_(ParseError::NO_ERROR)
    {
        yyjson_read_err read_err;
        doc_ = yyjson_read_opts(
            const_cast<char*>(json_data),
            json_len,
            0,
            nullptr,
            &read_err
        );
        
        if (!doc_) {
            err_ = ParseError::ILLFORMED_OBJECT;
        } else {
            root_ = yyjson_doc_get_root(doc_);
            current_ = root_;
        }
    }
    
    // Destructor - frees document if owned
    ~YyjsonReader() {
        if (owns_doc_ && doc_) {
            yyjson_doc_free(doc_);
        }
    }
    
    // Non-copyable (may own resources)
    YyjsonReader(const YyjsonReader&) = delete;
    YyjsonReader& operator=(const YyjsonReader&) = delete;
    
    // Movable
    YyjsonReader(YyjsonReader&& other) noexcept
        : root_(other.root_)
        , current_(other.current_)
        , doc_(other.doc_)
        , owns_doc_(other.owns_doc_)
        , err_(other.err_)
    {
        other.doc_ = nullptr;
        other.owns_doc_ = false;
    }

    // ---- Introspection ----

    iterator_type current() const noexcept {
        // For yyjson backend we don't know exact char position; use node ptr.
        return current_;
    }

    ParseError getError() const noexcept { return err_; }

    // ---- Scalars ----

    reader::TryParseStatus start_value_and_try_read_null() {
        if (!current_) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return reader::TryParseStatus::error;
        }
        if (!yyjson_is_null(current_)) {
            return reader::TryParseStatus::no_match;
        }
        return reader::TryParseStatus::ok;
    }

    reader::TryParseStatus read_bool(bool& b) {
        if (!current_) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return reader::TryParseStatus::error;
        }
        if (!yyjson_is_bool(current_)) {
            return reader::TryParseStatus::no_match;
        }
        b = yyjson_get_bool(current_) != 0;
        return reader::TryParseStatus::ok;
    }

    template<class NumberT>
    reader::TryParseStatus read_number(NumberT& storage) {
        if (!current_) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return reader::TryParseStatus::error;
        }
        if (!yyjson_is_num(current_)) {
            return reader::TryParseStatus::no_match;
        }


        if constexpr (std::is_integral_v<NumberT>) {
            if (yyjson_is_int(current_)) {
                auto v = yyjson_get_sint(current_);
                if (v < std::int64_t(std::numeric_limits<NumberT>::lowest()) ||
                    v > std::int64_t(std::numeric_limits<NumberT>::max())) {
                    setError(ParseError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                    return reader::TryParseStatus::error;
                }
                storage = static_cast<NumberT>(v);
            } else if (yyjson_is_uint(current_)) {
                auto v = yyjson_get_uint(current_);
                if (v < std::uint64_t(std::numeric_limits<NumberT>::lowest()) ||
                    v > std::uint64_t(std::numeric_limits<NumberT>::max())) {
                    setError(ParseError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                    return reader::TryParseStatus::error;
                }
                storage = static_cast<NumberT>(v);
            } else {
                auto v = yyjson_get_real(current_);
                if (v < double(std::numeric_limits<NumberT>::lowest()) ||
                    v > double(std::numeric_limits<NumberT>::max())) {
                    setError(ParseError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                    return reader::TryParseStatus::error;
                }
                storage = static_cast<NumberT>(v);
            }
        } else {
            static_assert(std::is_floating_point_v<NumberT>,
                          "read_number only supports integral or floating types");

            double v;
            if (yyjson_is_real(current_)) {
                v = yyjson_get_real(current_);
            } else if (yyjson_is_sint(current_)) {
                v = static_cast<double>(yyjson_get_sint(current_));
            } else {
                v = static_cast<double>(yyjson_get_uint(current_));
            }

            if (v < double(std::numeric_limits<NumberT>::lowest()) ||
                v > double(std::numeric_limits<NumberT>::max())) {
                setError(ParseError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                return reader::TryParseStatus::error;
            }
            storage = static_cast<NumberT>(v);
        }
        return reader::TryParseStatus::ok;

    }

    // ---- String reader (for both keys and values) ----
    //
    // Protocol:
    //  - For keys: current_ must point to the key node.
    //  - For values: current_ must point to the value node.
    //  The parser / object frame logic is responsible for setting current_.

    reader::StringChunkResult read_string_chunk(char* out, std::size_t capacity) {
        reader::StringChunkResult res{};
        res.status = reader::StringChunkStatus::error;
        res.bytes_written = 0;
        res.done = false;

        if (capacity == 0) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return res;
        }

        if (!value_str_active_) {
            if (!current_ || !yyjson_is_str(current_)) {
                res.status = reader::StringChunkStatus::no_match;
                return res;
            }

            value_str_data_   = yyjson_get_str(current_);
            value_str_len_    = yyjson_get_len(current_);
            value_str_offset_ = 0;
            value_str_active_ = true;
        }

        const std::size_t remaining = value_str_len_ - value_str_offset_;
        const std::size_t n         = remaining < capacity ? remaining : capacity;

        std::memcpy(out, value_str_data_ + value_str_offset_, n);
        value_str_offset_ += n;

        res.status        = reader::StringChunkStatus::ok;
        res.bytes_written = n;
        res.done          = (value_str_offset_ >= value_str_len_);

        if (res.done) {
            reset_value_string_state();
        }

        return res;
    }

    bool read_key_as_index(std::size_t & out) {
        constexpr std::size_t bufSize = 32;

        char buf[bufSize];
        if(reader::StringChunkResult r = read_string_chunk(buf, bufSize-1); !r.done || r.status != reader::StringChunkStatus::ok) {
            return false;
        } else {
            buf[r.bytes_written] = 0;
            auto [ptr, ec] = std::from_chars(buf, buf+r.bytes_written, out);
            if(ec != std::errc()) {
                setError(ParseError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                return false;
            } else {
                return true;
            }
        }
    }

    // ---- Arrays (new frame-based API) ----

    // Parser: creates ArrayFrame on its stack and calls this.
    reader::IterationStatus read_array_begin(ArrayFrame& frame) {
        reset_value_string_state();

        reader::IterationStatus ret;
        if (!current_){
            setError(ParseError::ILLFORMED_ARRAY);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        if(!yyjson_is_arr(current_)) {
            ret.status = reader::TryParseStatus::no_match;
            return ret;
        }

        yyjson_val* arr = current_;
        frame.arr   = arr;
        frame.size  = yyjson_arr_size(arr);
        frame.index = 0;
        frame.current = nullptr;

        if (frame.size > 0) {
            yyjson_arr_iter_init(arr, &frame.it);
            frame.current = yyjson_arr_iter_next(&frame.it);
            current_ = frame.current; // first element
            ret.has_value = true;
        } else {
            // Empty array – keep current_ on the array node.
            current_ = frame.arr;
            ret.has_value = false;
        }
        ret.status = reader::TryParseStatus::ok;

        return ret;
    }

    reader::IterationStatus advance_after_value(ArrayFrame& frame) {
        reset_value_string_state();
        reader::IterationStatus ret;

        if (!frame.arr) {
            setError(ParseError::ILLFORMED_ARRAY);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        ++frame.index;
        if (frame.index < frame.size) {
            // Move to next element.
            frame.current = yyjson_arr_iter_next(&frame.it);
            current_ = frame.current;
            ret.has_value = true;
        } else {
            // Last element finished; current_ becomes the array node.
            frame.current = nullptr;
            current_ = frame.arr;
            ret.has_value = false;
        }
        ret.status = reader::TryParseStatus::ok;
        return ret;
    }


    reader::IterationStatus read_map_begin(MapFrame& frame) {
        reset_value_string_state();
        reader::IterationStatus ret;

        if (!current_){
            setError(ParseError::ILLFORMED_ARRAY);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }
        if(!yyjson_is_obj(current_)) {
            ret.status = reader::TryParseStatus::no_match;
            return ret;
        }

        yyjson_val* obj = current_;
        frame.obj       = obj;
        frame.key       = nullptr;
        frame.value     = nullptr;

        yyjson_obj_iter_init(obj, &frame.it);

        // Preload first member, if any.
        if (!advance_object_member(frame)) {
            current_ = frame.obj;
            ret.has_value = false;
        } else {
            // we have a first key; set current_ to that key for key-string reading.
            current_ = frame.key;
            ret.has_value = true;
        }

        ret.status = reader::TryParseStatus::ok;
        return ret;
    }


    // Parser: after it finishes reading the key string, it calls this
    // to switch from key → value context.
    bool move_to_value(MapFrame& frame) {
        reset_value_string_state();

        if (!frame.obj) return true;
        if (!frame.value) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return false;
        }

        current_ = frame.value;
        return true;
    }

    // Parser: after the value is parsed, it calls this to move to the next member.
    reader::IterationStatus advance_after_value(MapFrame& frame) {
        reset_value_string_state();

        reader::IterationStatus ret;

        if (!frame.obj) {
            setError(ParseError::ILLFORMED_OBJECT);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        // We just finished reading frame.value.
        if (!advance_object_member(frame)) {
            // No more members → we're effectively at '}'.
            current_ = frame.obj;
            ret.has_value = false;
        } else {
            // Moved to next key.
            current_ = frame.key;
            ret.has_value = true;
        }
        ret.status = reader::TryParseStatus::ok;

        return ret;
    }

    // ---- Skip support ----

    bool skip_value()
    {
        // DOM is already built; for yyjson, "skip" means "don't materialize".
        return true;
    }

    bool finish() {
        return true;
    }
    
    // WireSink support - stores DOM node handle (O(1) operation)
    template<WireSinkLike Sink>
    bool capture_to_sink(Sink& sink) {
        if (!current_) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return false;
        }
        
        sink.clear();
        
        // Store only the node pointer - O(1) operation!
        // This is the main advantage of DOM-based readers with WireSink
        // Just 8 bytes (pointer size)
        // NOTE: The original yyjson_doc must remain alive while WireSink is in use!
        const char* node_bytes = reinterpret_cast<const char*>(&current_);
        if (!sink.write(node_bytes, sizeof(yyjson_val*))) {
            setError(ParseError::ILLFORMED_OBJECT); // Sink too small
            return false;
        }
        
        return true;
    }
    
    // Create a reader from WireSink
    // If sink contains node pointer (8 bytes) - extract pointer, non-owned reader
    // else error
    static YyjsonReader from_sink(const WireSinkLike auto& sink) {
        // Check if it's a node pointer (from yyjson capture)
        if (sink.current_size() == sizeof(yyjson_val*)) {
            // Extract node pointer - original document must still be alive!
            yyjson_val* node = nullptr;
            std::memcpy(&node, sink.data(), sizeof(yyjson_val*));
            return YyjsonReader(node);
        }
        
        // TODO error case, should not be here.
        return YyjsonReader(nullptr, 0);
    }

private:
    yyjson_val* root_   = nullptr;
    yyjson_val* current_ = nullptr;
    yyjson_doc* doc_    = nullptr;
    bool owns_doc_      = false;
    ParseError  err_    = ParseError::NO_ERROR;

    void setError(ParseError e) noexcept {
        if (err_ == ParseError::NO_ERROR) err_ = e;
    }

    // --- String chunk state for *value at current_* (keys or values) ---
    const char* value_str_data_   = nullptr;
    std::size_t value_str_len_    = 0;
    std::size_t value_str_offset_ = 0;
    bool        value_str_active_ = false;

    void reset_value_string_state() noexcept {
        value_str_data_   = nullptr;
        value_str_len_    = 0;
        value_str_offset_ = 0;
        value_str_active_ = false;
    }

    // Advance object iterator to next member.
    // On success:
    //   - frame.key / frame.value updated
    //   - returns true
    // On end:
    //   - frame.key = frame.value = nullptr
    //   - returns false
    bool advance_object_member(MapFrame& frame) {
        if (!frame.obj) return false;

        yyjson_val* key = yyjson_obj_iter_next(&frame.it);
        if (!key) {
            frame.key       = nullptr;
            frame.value     = nullptr;
            return false;
        }

        frame.key       = key;
        frame.value     = yyjson_obj_iter_get_val(key);
        return true;
    }
};

static_assert(reader::ReaderLike<YyjsonReader>);


class YyjsonWriter {
public:
    using iterator_type = yyjson_mut_val*;

    enum class Error {
        None,
        AllocFailed,
        InvalidState
    };

    using error_type = Error;

    struct ArrayFrame {
        yyjson_mut_val* node = nullptr;

        // link to parent "scope" (if any)
        void* parent_frame   = nullptr;
        bool  parent_is_map  = false;
    };

    struct MapFrame {
        yyjson_mut_val* node = nullptr;

        // parent scope
        void* parent_frame   = nullptr;
        bool  parent_is_map  = false;

        // key state
        bool        expecting_key = true;
        bool        use_index_key = false;
        std::size_t pending_index = 0;
        std::string pending_key;
    };

    // Constructor for external document (user manages lifetime)
    explicit YyjsonWriter(yyjson_mut_doc * doc)
        : doc_(doc)
        , root_(nullptr)
        , current_(nullptr)
        , error_(Error::None)
        , scope_kind_(ScopeKind::Root)
        , scope_frame_(nullptr)
        , owns_doc_(false)
        , output_sink_finisher_(nullptr)
    {
        if (!doc_) {
            error_ = Error::AllocFailed;
        }
    }
    
    // Constructor for owned document (RAII)
    YyjsonWriter()
        : doc_(yyjson_mut_doc_new(nullptr))
        , root_(nullptr)
        , current_(nullptr)
        , error_(doc_ ? Error::None : Error::AllocFailed)
        , scope_kind_(ScopeKind::Root)
        , scope_frame_(nullptr)
        , owns_doc_(true)
        , output_sink_finisher_(nullptr)
    {
    }
    
    // Constructor with string output (RAII + auto-serialize to string)
    explicit YyjsonWriter(std::string& output)
        : doc_(yyjson_mut_doc_new(nullptr))
        , root_(nullptr)
        , current_(nullptr)
        , error_(doc_ ? Error::None : Error::AllocFailed)
        , scope_kind_(ScopeKind::Root)
        , scope_frame_(nullptr)
        , owns_doc_(true)
        , output_sink_finisher_(nullptr)
    {
        // Create finisher that serializes to string
        output_sink_finisher_ = [&output](yyjson_mut_doc* doc) -> std::size_t {
            std::size_t json_len = 0;
            char* json_str = yyjson_mut_write(doc, 0, &json_len);
            if (!json_str) {
                return -1;
            }
            
            output.assign(json_str, json_len);
            free(json_str);
            return json_len;
        };
    }
    
    // Destructor - frees document if owned
    ~YyjsonWriter() {
        if (owns_doc_ && doc_) {
            yyjson_mut_doc_free(doc_);
        }
    }
    
    // Non-copyable (owns resources)
    YyjsonWriter(const YyjsonWriter&) = delete;
    YyjsonWriter& operator=(const YyjsonWriter&) = delete;
    
    // Movable
    YyjsonWriter(YyjsonWriter&& other) noexcept
        : doc_(other.doc_)
        , root_(other.root_)
        , current_(other.current_)
        , error_(other.error_)
        , scope_kind_(other.scope_kind_)
        , scope_frame_(other.scope_frame_)
        , owns_doc_(other.owns_doc_)
        , output_sink_finisher_(std::move(other.output_sink_finisher_))
    {
        other.doc_ = nullptr;
        other.owns_doc_ = false;
        other.output_sink_finisher_ = nullptr;
    }

    // ========== required API for WriterLike ==========

    iterator_type current() noexcept {
        return current_;
    }

    error_type getError() const noexcept {
        return error_;
    }

    // ---- containers ----

    bool write_array_begin(std::size_t const& /*size*/, ArrayFrame& frame) {
        if (!ensure_ok()) return false;
        yyjson_mut_val* arr = yyjson_mut_arr(doc_);
        if (!arr) return fail(Error::AllocFailed);

        // Attach array as a value of current scope
        if (!attach_value_to_current(arr)) {
            return false;
        }

        // Fill frame and switch scope to this array
        frame.node         = arr;
        frame.parent_frame = scope_frame_;
        frame.parent_is_map = (scope_kind_ == ScopeKind::Map);

        scope_kind_  = ScopeKind::Array;
        scope_frame_ = &frame;

        current_ = arr;
        return true;
    }

    bool write_map_begin(std::size_t const& /*size*/, MapFrame& frame) {
        if (!ensure_ok()) return false;

        yyjson_mut_val* obj = yyjson_mut_obj(doc_);
        if (!obj) return fail(Error::AllocFailed);

        // Attach map as a value of current scope
        if (!attach_value_to_current(obj)) {
            return false;
        }

        frame.node          = obj;
        frame.parent_frame  = scope_frame_;
        frame.parent_is_map = (scope_kind_ == ScopeKind::Map);

        frame.expecting_key = true;
        frame.use_index_key = false;
        frame.pending_index = 0;
        frame.pending_key.clear();

        scope_kind_  = ScopeKind::Map;
        scope_frame_ = &frame;

        current_ = obj;
        return true;
    }

    // “Separator” hook: called *between* elements.
    // For DOM / yyjson this is a no-op; commas are implicit in tree.
    bool advance_after_value(ArrayFrame& ) {
        return ensure_ok();
    }

    bool advance_after_value(MapFrame& ) {
        return ensure_ok();
    }

    // For textual JSON this would emit ':'. For yyjson we just sanity-check.
    bool move_to_value(MapFrame& frame) {
        if (!ensure_ok()) return false;
        if (scope_kind_ != ScopeKind::Map || scope_frame_ != &frame) {
            return fail(Error::InvalidState);
        }

        if (frame.expecting_key) {
            // value without key
            return fail(Error::InvalidState);
        }
        // nothing to do; next write_* will attach as value for pending key
        return true;
    }

    // String keys are handled by write_string() when in “key mode”.
    // write_key_as_index is for indexes_as_keys.
    bool write_key_as_index(std::size_t const& idx) {
        if (!ensure_ok()) return false;
        if (scope_kind_ != ScopeKind::Map) return fail(Error::InvalidState);

        MapFrame* frame = static_cast<MapFrame*>(scope_frame_);
        if (!frame->expecting_key) {
            return fail(Error::InvalidState);
        }

        frame->use_index_key  = true;
        frame->pending_index  = idx;
        frame->pending_key.clear();
        frame->expecting_key  = false;
        return true;
    }

    bool write_array_end(ArrayFrame& frame) {
        if (!ensure_ok()) return false;
        if (scope_kind_ != ScopeKind::Array || scope_frame_ != &frame) {
            return fail(Error::InvalidState);
        }

        // restore parent scope from frame
        restore_parent_scope(frame.parent_frame, frame.parent_is_map);
        return true;
    }

    bool write_map_end(MapFrame& frame) {
        if (!ensure_ok()) return false;
        if (scope_kind_ != ScopeKind::Map || scope_frame_ != &frame) {
            return fail(Error::InvalidState);
        }

        if (!frame.expecting_key) {
            // have a key without a value
            return fail(Error::InvalidState);
        }

        restore_parent_scope(frame.parent_frame, frame.parent_is_map);
        return true;
    }

    // ---- primitives ----

    bool write_null() {
        if (!ensure_ok()) return false;
        yyjson_mut_val* v = yyjson_mut_null(doc_);
        if (!v) return fail(Error::AllocFailed);
        return attach_value_to_current(v);
    }

    bool write_bool(bool const& b) {
        if (!ensure_ok()) return false;
        yyjson_mut_val* v = yyjson_mut_bool(doc_, b);
        if (!v) return fail(Error::AllocFailed);
        return attach_value_to_current(v);
    }

    template<class NumberT>
    bool write_number(NumberT const& value) {
        if (!ensure_ok()) return false;

        yyjson_mut_val* v = nullptr;
        if constexpr (std::is_floating_point_v<NumberT>) {
            v = yyjson_mut_real(doc_, static_cast<double>(value));
        } else if constexpr (std::is_signed_v<NumberT>) {
            v = yyjson_mut_sint(doc_, static_cast<int64_t>(value));
        } else if constexpr (std::is_unsigned_v<NumberT>) {
            v = yyjson_mut_uint(doc_, static_cast<uint64_t>(value));
        } else {
            static_assert(std::is_arithmetic_v<NumberT>,
                          "write_number only supports arithmetic types");
        }

        if (!v) return fail(Error::AllocFailed);
        return attach_value_to_current(v);
    }

    // Chunked string writing interface
    // For yyjson DOM builder, we buffer chunks then create the node on end
    bool write_string_begin(std::size_t /*size_hint*/) {
        if (!ensure_ok()) return false;
        string_buffer_.clear();
        return true;
    }
    
    bool write_string_chunk(char const* data, std::size_t size) {
        if (!ensure_ok()) return false;
        string_buffer_.append(data, size);
        return true;
    }
    
    bool write_string_end() {
        if (!ensure_ok()) return false;
        
        if (scope_kind_ == ScopeKind::Map) {
            MapFrame* frame = static_cast<MapFrame*>(scope_frame_);
            if (frame->expecting_key) {
                frame->pending_key = std::move(string_buffer_);
                frame->use_index_key  = false;
                frame->pending_index  = 0;
                frame->expecting_key  = false;
                string_buffer_.clear();
                return true;
            }
        }

        yyjson_mut_val* v = yyjson_mut_strncpy(doc_, string_buffer_.data(), string_buffer_.size());
        string_buffer_.clear();
        if (!v) return fail(Error::AllocFailed);
        return attach_value_to_current(v);
    }
    
    // String writing (convenience wrapper for single-call usage):
    //  - In map & expecting_key → record key in MapFrame
    //  - Else → create value string node and attach
    bool write_string(char const* data, std::size_t size, bool null_terminated = false) {
        if (!ensure_ok()) return false;

        if (null_terminated) {
            size = std::strlen(data);
        }

        if (scope_kind_ == ScopeKind::Map) {
            MapFrame* frame = static_cast<MapFrame*>(scope_frame_);
            if (frame->expecting_key) {
                frame->pending_key.assign(data, size);
                frame->use_index_key  = false;
                frame->pending_index  = 0;
                frame->expecting_key  = false;
                return true;
            }
        }

        yyjson_mut_val* v = yyjson_mut_strncpy(doc_, data, size);
        if (!v) return fail(Error::AllocFailed);
        return attach_value_to_current(v);
    }

    // ---- finish ----

    std::size_t finish() {
        if (!ensure_ok()) return -1;
        if (!doc_) {
            fail(Error::InvalidState);
            return -1;
        }

        if (!root_) {
            yyjson_mut_val* v = yyjson_mut_null(doc_);
            if (!v) {
                fail(Error::AllocFailed);
                return -1;
            }
            yyjson_mut_doc_set_root(doc_, v);
            root_ = v;
        }

        // If created via from_sink, serialize the document to the sink
        if(!output_sink_finisher_) {
            fail(Error::InvalidState);
            return -1;
        }

        std::size_t bytesWritten = output_sink_finisher_(doc_);
        if (bytesWritten == std::size_t(-1)) {
            fail(Error::InvalidState);
            return -1;
        }


        // You *could* assert here that scope_kind_ == Root, but
        // serializer already guarantees balanced begin/end.
        return bytesWritten;
    }
    
    // Cleanup callback for WireSink containing YyJSON document
    // Format: [yyjson_mut_doc* (8 bytes) | yyjson_mut_val* (8 bytes)]
    static void yyjson_mut_doc_cleanup(char* data, std::size_t size) {
        if (size == sizeof(yyjson_mut_doc*) + sizeof(yyjson_mut_val*)) {
            yyjson_mut_doc* doc = nullptr;
            std::memcpy(&doc, data, sizeof(doc));
            if (doc) {
                yyjson_mut_doc_free(doc);
            }
        }
    }

    // Create a writer from WireSink - builds DOM, stores [doc*, node*] in sink with RAII cleanup
    // O(1) capture: stores pointers, WireSink owns document via cleanup callback
    // Immutable: can output multiple times (copies node each time)
    template<WireSinkLike Sink>
    static YyjsonWriter from_sink(Sink& sink) {
        YyjsonWriter writer;  // Creates owned document
        writer.owns_doc_ = false;  // Ownership transferred to sink
        
        // Finisher stores [doc*, node*] and sets cleanup
        // WireSink takes ownership via cleanup callback
        writer.output_sink_finisher_ = [&sink](yyjson_mut_doc* doc) -> std::size_t {
            sink.clear();
            
            // Get root node
            yyjson_mut_val* root = yyjson_mut_doc_get_root(doc);
            if (!root) {
                return -1;
            }
            
            // Check buffer size
            constexpr std::size_t needed = sizeof(yyjson_mut_doc*) + sizeof(yyjson_mut_val*);
            if (needed > sink.max_size()) {
                return false;
            }
            
            // Store [doc*, node*] - WireSink takes ownership
            const char* doc_bytes = reinterpret_cast<const char*>(&doc);
            if (!sink.write(doc_bytes, sizeof(yyjson_mut_doc*))) {
                return -1;
            }
            
            const char* node_bytes = reinterpret_cast<const char*>(&root);
            if (!sink.write(node_bytes, sizeof(yyjson_mut_val*))) {
                return -1;
            }
            
            // Set cleanup: WireSink will free doc on destruction/clear
            sink.set_cleanup(yyjson_mut_doc_cleanup);
            
            return needed;
        };
        
        return writer;
    }
    
    // WireSink support - retrieves data from sink and adds to mutable doc
    // Protocol-consistent: YyjsonWriter only receives data from YyjsonReader/YyjsonWriter
    template<WireSinkLike Sink>
    bool output_from_sink(const Sink& sink) {
        if (!ensure_ok()) return false;
        
        yyjson_val* source_immut_node = nullptr;
        
        if (sink.current_size() == sizeof(yyjson_val*)) {
            // Case 1: Immutable node pointer (from YyjsonReader::capture_to_sink)
            // Format: [yyjson_val*]
            // O(1) - just a pointer reference, original doc must stay alive
            std::memcpy(&source_immut_node, sink.data(), sizeof(yyjson_val*));
            
        } else if (sink.current_size() == sizeof(yyjson_mut_doc*) + sizeof(yyjson_mut_val*)) {
            // Case 2: Mutable [doc*, node*] (from YyjsonWriter::from_sink in transformer)
            // Format: [yyjson_mut_doc* | yyjson_mut_val*]
            // O(1) - WireSink owns the doc (RAII cleanup), we just copy the node
            yyjson_mut_val* source_mut_node = nullptr;
            std::memcpy(&source_mut_node, sink.data() + sizeof(yyjson_mut_doc*), sizeof(source_mut_node));
            
            // Cast to immutable for copying (yyjson_val_mut_copy accepts immutable source)
            source_immut_node = reinterpret_cast<yyjson_val*>(source_mut_node);
            
            // Note: We do NOT free the doc - WireSink owns it via cleanup callback
            
        } else {
            // Unknown format - protocol mismatch or corrupted data
            return fail(Error::InvalidState);
        }
        
        if (!source_immut_node) {
            return fail(Error::InvalidState);
        }
        
        // Copy node to this writer's mutable doc
        yyjson_mut_val* mut_val = yyjson_val_mut_copy(doc_, source_immut_node);
        if (!mut_val) {
            return fail(Error::AllocFailed);
        }
        
        // Attach to current scope
        if (!attach_value_to_current(mut_val)) {
            return false;
        }
        
        current_ = mut_val;
        return true;
    }


private:
    enum class ScopeKind { Root, Array, Map };

    yyjson_mut_doc* doc_;
    yyjson_mut_val* root_;
    yyjson_mut_val* current_;
    error_type      error_;

    ScopeKind scope_kind_;
    void*     scope_frame_;
    
    // Buffer for chunked string writing
    std::string string_buffer_;
    
    // RAII and sink support
    bool  owns_doc_;           // True if we should free doc_ in destructor
    std::function<std::size_t(yyjson_mut_doc*)> output_sink_finisher_;  // Finisher lambda for from_sink

    bool ensure_ok() const noexcept {
        return error_ == Error::None;
    }

    bool fail(Error e) {
        if (error_ == Error::None) {
            error_ = e;
        }
        return false;
    }

    void restore_parent_scope(void* parent_frame, bool parent_is_map) {
        if (!parent_frame) {
            scope_kind_  = ScopeKind::Root;
            scope_frame_ = nullptr;
        } else {
            scope_kind_  = parent_is_map ? ScopeKind::Map : ScopeKind::Array;
            scope_frame_ = parent_frame;
        }
    }

    bool attach_value_to_current(yyjson_mut_val* v) {
        if (!ensure_ok()) return false;

        switch (scope_kind_) {
        case ScopeKind::Root:
            if (root_) {
                return fail(Error::InvalidState); // multiple roots
            }
            root_ = v;
            yyjson_mut_doc_set_root(doc_, v);
            break;

        case ScopeKind::Array: {
            auto* frame = static_cast<ArrayFrame*>(scope_frame_);
            if (!frame || !frame->node) return fail(Error::InvalidState);
            if (!yyjson_mut_arr_add_val(frame->node, v)) {
                return fail(Error::AllocFailed);
            }
            break;
        }

        case ScopeKind::Map: {
            auto* frame = static_cast<MapFrame*>(scope_frame_);
            if (!frame || !frame->node) return fail(Error::InvalidState);
            if (frame->expecting_key) {
                // got a value while still waiting for a key
                return fail(Error::InvalidState);
            }

            yyjson_mut_val* key_node = nullptr;
            if (frame->use_index_key) {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), frame->pending_index);
                if (ec != std::errc()) {
                    return fail(Error::InvalidState);
                }
                std::size_t len = static_cast<std::size_t>(ptr - buf);
                key_node = yyjson_mut_strncpy(doc_, buf, len);
                frame->use_index_key = false;
                frame->pending_index = 0;
            } else {
                key_node = yyjson_mut_strncpy(doc_,
                                              frame->pending_key.data(),
                                              frame->pending_key.size());
                frame->pending_key.clear();
            }

            if (!key_node) return fail(Error::AllocFailed);
            if (!yyjson_mut_obj_add(frame->node, key_node, v)) {
                return fail(Error::AllocFailed);
            }
            frame->expecting_key = true;
            break;
        }
        }

        current_ = v;
        return true;
    }
};
static_assert(writer::WriterLike<YyjsonWriter>);
} // namespace JsonFusion

#endif // __has_include(<yyjson.h>)
