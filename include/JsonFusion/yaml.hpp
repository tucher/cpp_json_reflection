#pragma once

// Optional YAML backend (rapidyaml). Guarded so this header is always safe to
// include: it compiles to nothing unless <rapidyaml.hpp> is on the include path.
#if __has_include(<rapidyaml.hpp>)
#include <rapidyaml.hpp>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <charconv>
#include <functional>
#include <limits>
#include "reader_concept.hpp"
#include "writer_concept.hpp"

namespace JsonFusion {

class RapidYamlReader {
public:
    enum class ParseError {
        NO_ERROR,
        UNEXPECTED_END_OF_DATA,
        ILLFORMED_OBJECT,
        ILLFORMED_ARRAY,
        NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE,
        TYPE_MISMATCH,
        UNSUPPORTED_YAML_FEATURE  // anchors, aliases, tags, multi-doc
    };
    using error_type = ParseError;

    using iterator_type = ryml::ConstNodeRef;

    struct ArrayFrame {
        ryml::ConstNodeRef node{};
        std::size_t        index = 0;
        std::size_t        size  = 0;
        ryml::ConstNodeRef current{};
    };

    struct MapFrame {
        ryml::ConstNodeRef node{};
        std::size_t        index = 0;
        std::size_t        size  = 0;
        ryml::ConstNodeRef current_child{};  // current key-value pair node
    };

    // Constructor for external tree (user manages lifetime)
    explicit RapidYamlReader(ryml::ConstNodeRef root) noexcept
        : root_(root)
        , current_(root)
        , tree_(nullptr)
        , owns_tree_(false)
        , err_(ParseError::NO_ERROR)
    {
        if (root_.readable()) {
            checkUnsupportedFeatures(root_);
        }
    }

    // Constructor that parses YAML and owns tree (RAII)
    explicit RapidYamlReader(const char* yaml_data, std::size_t yaml_len) noexcept
        : root_()
        , current_()
        , tree_(nullptr)
        , owns_tree_(true)
        , err_(ParseError::NO_ERROR)
    {
        try {
            tree_ = new ryml::Tree();
            *tree_ = ryml::parse_in_arena(c4::csubstr(yaml_data, yaml_len));

            // Check for multi-document (not supported)
            if (tree_->rootref().num_children() > 1 && tree_->rootref().is_stream()) {
                setError(ParseError::UNSUPPORTED_YAML_FEATURE);
                return;
            }

            root_ = tree_->rootref();
            // If root is a stream with single doc, descend into it
            if (root_.is_stream() && root_.num_children() == 1) {
                root_ = root_.first_child();
            }
            current_ = root_;

            if (root_.readable()) {
                checkUnsupportedFeatures(root_);
            }
        } catch (...) {
            err_ = ParseError::ILLFORMED_OBJECT;
        }
    }

    // Destructor
    ~RapidYamlReader() {
        if (owns_tree_ && tree_) {
            delete tree_;
        }
    }

    // Non-copyable
    RapidYamlReader(const RapidYamlReader&) = delete;
    RapidYamlReader& operator=(const RapidYamlReader&) = delete;

    // Movable
    RapidYamlReader(RapidYamlReader&& other) noexcept
        : root_(other.root_)
        , current_(other.current_)
        , tree_(other.tree_)
        , owns_tree_(other.owns_tree_)
        , err_(other.err_)
    {
        other.tree_ = nullptr;
        other.owns_tree_ = false;
    }

    // ---- Introspection ----

    iterator_type current() const noexcept {
        return current_;
    }

    ParseError getError() const noexcept { return err_; }

    // ---- Scalars ----

    reader::TryParseStatus start_value_and_try_read_null() {
        if (!current_.readable()) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return reader::TryParseStatus::error;
        }

        if (!current_.is_val() && !current_.has_val()) {
            return reader::TryParseStatus::no_match;
        }

        c4::csubstr val = current_.val();
        if (val == "null" || val == "~" || val.empty()) {
            // Check if it's truly null vs empty string
            // In YAML, ~ and null are null, empty unquoted is null
            // But "" (quoted empty) is empty string
            if (val == "null" || val == "~") {
                return reader::TryParseStatus::ok;
            }
            // For empty, check if it was quoted
            if (val.empty() && !current_.is_val_quoted()) {
                return reader::TryParseStatus::ok;
            }
        }
        return reader::TryParseStatus::no_match;
    }

    reader::TryParseStatus read_bool(bool& b) {
        if (!current_.readable()) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return reader::TryParseStatus::error;
        }

        if (!current_.is_val() && !current_.has_val()) {
            return reader::TryParseStatus::no_match;
        }

        c4::csubstr val = current_.val();

        // JSON-like: only accept true/false, not yes/no/on/off
        if (val == "true") {
            b = true;
            return reader::TryParseStatus::ok;
        }
        if (val == "false") {
            b = false;
            return reader::TryParseStatus::ok;
        }

        return reader::TryParseStatus::no_match;
    }

    template<class NumberT>
    reader::TryParseStatus read_number(NumberT& storage) {
        if (!current_.readable()) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return reader::TryParseStatus::error;
        }

        if (!current_.is_val() && !current_.has_val()) {
            return reader::TryParseStatus::no_match;
        }

        c4::csubstr val = current_.val();
        if (val.empty()) {
            return reader::TryParseStatus::no_match;
        }

        // Check if it looks like a number
        char first = val[0];
        if (!((first >= '0' && first <= '9') || first == '-' || first == '+' || first == '.')) {
            return reader::TryParseStatus::no_match;
        }

        if constexpr (std::is_integral_v<NumberT>) {
            // Try parsing as integer
            NumberT result{};
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), result);

            if (ec == std::errc::result_out_of_range) {
                setError(ParseError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                return reader::TryParseStatus::error;
            }

            if (ec != std::errc() || ptr != val.data() + val.size()) {
                // Maybe it's a float that we need to truncate?
                // For strict JSON-like behavior, try parsing as double first
                double d{};
                auto [ptr2, ec2] = std::from_chars(val.data(), val.data() + val.size(), d);
                if (ec2 == std::errc() && ptr2 == val.data() + val.size()) {
                    if (d < static_cast<double>(std::numeric_limits<NumberT>::lowest()) ||
                        d > static_cast<double>(std::numeric_limits<NumberT>::max())) {
                        setError(ParseError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                        return reader::TryParseStatus::error;
                    }
                    storage = static_cast<NumberT>(d);
                    return reader::TryParseStatus::ok;
                }
                return reader::TryParseStatus::no_match;
            }

            storage = result;
            return reader::TryParseStatus::ok;

        } else {
            static_assert(std::is_floating_point_v<NumberT>,
                          "read_number only supports integral or floating types");

            double d{};
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), d);

            if (ec != std::errc() || ptr != val.data() + val.size()) {
                return reader::TryParseStatus::no_match;
            }

            if (d < static_cast<double>(std::numeric_limits<NumberT>::lowest()) ||
                d > static_cast<double>(std::numeric_limits<NumberT>::max())) {
                setError(ParseError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
                return reader::TryParseStatus::error;
            }

            storage = static_cast<NumberT>(d);
            return reader::TryParseStatus::ok;
        }
    }

    // ---- String reader ----

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
            if (!current_.readable()) {
                res.status = reader::StringChunkStatus::no_match;
                return res;
            }

            // For map keys, current_ points to the child node, use key()
            // For values, use val()
            c4::csubstr str;
            if (reading_key_) {
                if (!current_.has_key()) {
                    res.status = reader::StringChunkStatus::no_match;
                    return res;
                }
                str = current_.key();
            } else {
                if (!current_.is_val() && !current_.has_val()) {
                    res.status = reader::StringChunkStatus::no_match;
                    return res;
                }
                str = current_.val();

                // Check if it's actually a string (not null/bool/number)
                // For quoted strings, it's definitely a string
                // For unquoted, we need to check it's not a special value
                if (!current_.is_val_quoted()) {
                    if (str == "null" || str == "~" || str == "true" || str == "false") {
                        res.status = reader::StringChunkStatus::no_match;
                        return res;
                    }
                    // Check if it looks like a number
                    if (!str.empty()) {
                        char first = str[0];
                        if ((first >= '0' && first <= '9') || first == '-' || first == '+') {
                            // Could be a number, try to parse
                            double d{};
                            auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), d);
                            if (ec == std::errc() && ptr == str.data() + str.size()) {
                                res.status = reader::StringChunkStatus::no_match;
                                return res;
                            }
                        }
                    }
                }
            }

            value_str_data_   = str.data();
            value_str_len_    = str.size();
            value_str_offset_ = 0;
            value_str_active_ = true;
        }

        const std::size_t remaining = value_str_len_ - value_str_offset_;
        const std::size_t n         = remaining < capacity ? remaining : capacity;

        if (n > 0) {
            std::memcpy(out, value_str_data_ + value_str_offset_, n);
        }
        value_str_offset_ += n;

        res.status        = reader::StringChunkStatus::ok;
        res.bytes_written = n;
        res.done          = (value_str_offset_ >= value_str_len_);

        if (res.done) {
            reset_value_string_state();
        }

        return res;
    }

    bool read_key_as_index(std::size_t& out) {
        constexpr std::size_t bufSize = 32;

        reading_key_ = true;
        char buf[bufSize];
        reader::StringChunkResult r = read_string_chunk(buf, bufSize - 1);
        reading_key_ = false;

        if (!r.done || r.status != reader::StringChunkStatus::ok) {
            return false;
        }

        buf[r.bytes_written] = 0;
        auto [ptr, ec] = std::from_chars(buf, buf + r.bytes_written, out);
        if (ec != std::errc()) {
            setError(ParseError::NUMERIC_VALUE_IS_OUT_OF_STORAGE_TYPE_RANGE);
            return false;
        }
        return true;
    }

    // ---- Arrays ----

    reader::IterationStatus read_array_begin(ArrayFrame& frame) {
        reset_value_string_state();

        reader::IterationStatus ret;
        if (!current_.readable()) {
            setError(ParseError::ILLFORMED_ARRAY);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        if (!current_.is_seq()) {
            ret.status = reader::TryParseStatus::no_match;
            return ret;
        }

        if (hasUnsupportedFeatures(current_)) {
            setError(ParseError::UNSUPPORTED_YAML_FEATURE);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        frame.node    = current_;
        frame.size    = current_.num_children();
        frame.index   = 0;
        frame.current = ryml::ConstNodeRef{};

        if (frame.size > 0) {
            frame.current = current_.first_child();
            current_ = frame.current;
            ret.has_value = true;
        } else {
            ret.has_value = false;
        }
        ret.status = reader::TryParseStatus::ok;

        return ret;
    }

    reader::IterationStatus advance_after_value(ArrayFrame& frame) {
        reset_value_string_state();
        reader::IterationStatus ret;

        if (!frame.node.readable()) {
            setError(ParseError::ILLFORMED_ARRAY);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        ++frame.index;
        if (frame.index < frame.size) {
            frame.current = frame.current.next_sibling();
            current_ = frame.current;
            ret.has_value = true;
        } else {
            frame.current = ryml::ConstNodeRef{};
            current_ = frame.node;
            ret.has_value = false;
        }
        ret.status = reader::TryParseStatus::ok;
        return ret;
    }

    // ---- Maps ----

    reader::IterationStatus read_map_begin(MapFrame& frame) {
        reset_value_string_state();
        reader::IterationStatus ret;

        if (!current_.readable()) {
            setError(ParseError::ILLFORMED_OBJECT);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        if (!current_.is_map()) {
            ret.status = reader::TryParseStatus::no_match;
            return ret;
        }

        if (hasUnsupportedFeatures(current_)) {
            setError(ParseError::UNSUPPORTED_YAML_FEATURE);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        frame.node          = current_;
        frame.size          = current_.num_children();
        frame.index         = 0;
        frame.current_child = ryml::ConstNodeRef{};

        if (frame.size > 0) {
            frame.current_child = current_.first_child();
            current_ = frame.current_child;  // point to first key-value node
            reading_key_ = true;  // next string read will be key
            ret.has_value = true;
        } else {
            current_ = frame.node;
            ret.has_value = false;
        }

        ret.status = reader::TryParseStatus::ok;
        return ret;
    }

    bool move_to_value(MapFrame& frame) {
        reset_value_string_state();

        if (!frame.node.readable()) return true;
        if (!frame.current_child.readable()) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return false;
        }

        reading_key_ = false;
        // current_ already points to the child node which has both key and val
        return true;
    }

    reader::IterationStatus advance_after_value(MapFrame& frame) {
        reset_value_string_state();

        reader::IterationStatus ret;

        if (!frame.node.readable()) {
            setError(ParseError::ILLFORMED_OBJECT);
            ret.status = reader::TryParseStatus::error;
            return ret;
        }

        ++frame.index;
        if (frame.index < frame.size) {
            frame.current_child = frame.current_child.next_sibling();
            current_ = frame.current_child;
            reading_key_ = true;
            ret.has_value = true;
        } else {
            frame.current_child = ryml::ConstNodeRef{};
            current_ = frame.node;
            reading_key_ = false;
            ret.has_value = false;
        }
        ret.status = reader::TryParseStatus::ok;

        return ret;
    }

    // ---- Skip/Finish ----

    bool skip_value() {
        // DOM is already built; skip is a no-op
        return true;
    }

    bool finish() {
        return true;
    }

    // ---- WireSink support ----

    template<typename Sink>
    requires WireSinkLike<Sink>
    bool capture_to_sink(Sink& sink) {
        if (!current_.readable()) {
            setError(ParseError::UNEXPECTED_END_OF_DATA);
            return false;
        }

        sink.clear();

        // Store the node ID and tree pointer
        // Format: [ryml::Tree* (8 bytes) | ryml::id_type (4 bytes)]
        struct CaptureData {
            const ryml::Tree* tree;
            ryml::id_type node_id;
        };

        CaptureData data;
        data.tree = current_.tree();
        data.node_id = current_.id();

        const char* bytes = reinterpret_cast<const char*>(&data);
        if (!sink.write(bytes, sizeof(CaptureData))) {
            setError(ParseError::ILLFORMED_OBJECT);
            return false;
        }

        return true;
    }

    // Create reader from WireSink
    static RapidYamlReader from_sink(const WireSinkLike auto& sink) {
        struct CaptureData {
            const ryml::Tree* tree;
            ryml::id_type node_id;
        };

        if (sink.current_size() == sizeof(CaptureData)) {
            CaptureData data;
            std::memcpy(&data, sink.data(), sizeof(CaptureData));
            if (data.tree) {
                return RapidYamlReader(data.tree->cref(data.node_id));
            }
        }

        // Error case
        RapidYamlReader reader(ryml::ConstNodeRef{});
        reader.setError(ParseError::ILLFORMED_OBJECT);
        return reader;
    }

private:
    ryml::ConstNodeRef root_{};
    ryml::ConstNodeRef current_{};
    ryml::Tree*        tree_      = nullptr;
    bool               owns_tree_ = false;
    ParseError         err_       = ParseError::NO_ERROR;

    // String chunk state
    const char* value_str_data_   = nullptr;
    std::size_t value_str_len_    = 0;
    std::size_t value_str_offset_ = 0;
    bool        value_str_active_ = false;
    bool        reading_key_      = false;

    void setError(ParseError e) noexcept {
        if (err_ == ParseError::NO_ERROR) err_ = e;
    }

    void reset_value_string_state() noexcept {
        value_str_data_   = nullptr;
        value_str_len_    = 0;
        value_str_offset_ = 0;
        value_str_active_ = false;
        reading_key_      = false;
    }

    // Check for unsupported YAML features
    bool hasUnsupportedFeatures(ryml::ConstNodeRef node) const {
        if (!node.readable()) return false;

        // Check for anchors
        if (node.has_anchor()) return true;

        // Check for aliases (references)
        if (node.is_ref()) return true;

        // Check for tags
        if (node.has_key_tag() || node.has_val_tag()) return true;

        return false;
    }

    void checkUnsupportedFeatures(ryml::ConstNodeRef node) {
        if (hasUnsupportedFeatures(node)) {
            setError(ParseError::UNSUPPORTED_YAML_FEATURE);
            return;
        }

        // Recursively check children
        if (node.is_container()) {
            for (auto child : node.children()) {
                checkUnsupportedFeatures(child);
                if (err_ != ParseError::NO_ERROR) return;
            }
        }
    }
};

static_assert(reader::ReaderLike<RapidYamlReader>);


class RapidYamlWriter {
public:
    using iterator_type = ryml::NodeRef;

    enum class Error {
        None,
        AllocFailed,
        InvalidState
    };

    using error_type = Error;

    struct ArrayFrame {
        ryml::NodeRef node{};
        void*         parent_frame   = nullptr;
        bool          parent_is_map  = false;
    };

    struct MapFrame {
        ryml::NodeRef   node{};
        void*           parent_frame   = nullptr;
        bool            parent_is_map  = false;
        bool            expecting_key  = true;
        bool            use_index_key  = false;
        std::size_t     pending_index  = 0;
        std::string     pending_key;
    };

    // Constructor for owned tree (RAII)
    RapidYamlWriter()
        : tree_(new ryml::Tree())
        , root_()
        , current_()
        , error_(Error::None)
        , scope_kind_(ScopeKind::Root)
        , scope_frame_(nullptr)
        , owns_tree_(true)
        , output_sink_finisher_(nullptr)
    {
    }

    // Constructor with string output (RAII + auto-serialize)
    explicit RapidYamlWriter(std::string& output)
        : tree_(new ryml::Tree())
        , root_()
        , current_()
        , error_(Error::None)
        , scope_kind_(ScopeKind::Root)
        , scope_frame_(nullptr)
        , owns_tree_(true)
        , output_sink_finisher_(nullptr)
    {
        output_sink_finisher_ = [&output](ryml::Tree* tree) -> std::size_t {
            if (!tree) return static_cast<std::size_t>(-1);
            output = ryml::emitrs_yaml<std::string>(*tree);
            return output.size();
        };
    }

    // Destructor
    ~RapidYamlWriter() {
        if (owns_tree_ && tree_) {
            delete tree_;
        }
    }

    // Non-copyable
    RapidYamlWriter(const RapidYamlWriter&) = delete;
    RapidYamlWriter& operator=(const RapidYamlWriter&) = delete;

    // Movable
    RapidYamlWriter(RapidYamlWriter&& other) noexcept
        : tree_(other.tree_)
        , root_(other.root_)
        , current_(other.current_)
        , error_(other.error_)
        , scope_kind_(other.scope_kind_)
        , scope_frame_(other.scope_frame_)
        , owns_tree_(other.owns_tree_)
        , output_sink_finisher_(std::move(other.output_sink_finisher_))
    {
        other.tree_ = nullptr;
        other.owns_tree_ = false;
        other.output_sink_finisher_ = nullptr;
    }

    // ========== Required API for WriterLike ==========

    iterator_type current() noexcept {
        return current_;
    }

    error_type getError() const noexcept {
        return error_;
    }

    // ---- Containers ----

    bool write_array_begin(std::size_t const& /*size*/, ArrayFrame& frame) {
        if (!ensure_ok()) return false;

        ryml::NodeRef seq;
        if (scope_kind_ == ScopeKind::Root) {
            seq = tree_->rootref();
            seq |= ryml::SEQ;
        } else {
            seq = attach_container_to_current(ryml::SEQ);
            if (!seq.readable()) return false;
        }

        frame.node         = seq;
        frame.parent_frame = scope_frame_;
        frame.parent_is_map = (scope_kind_ == ScopeKind::Map);

        scope_kind_  = ScopeKind::Array;
        scope_frame_ = &frame;
        current_     = seq;

        if (scope_kind_ == ScopeKind::Root || frame.parent_frame == nullptr) {
            root_ = seq;
        }

        return true;
    }

    bool write_map_begin(std::size_t const& /*size*/, MapFrame& frame) {
        if (!ensure_ok()) return false;

        ryml::NodeRef map;
        if (scope_kind_ == ScopeKind::Root) {
            map = tree_->rootref();
            map |= ryml::MAP;
        } else {
            map = attach_container_to_current(ryml::MAP);
            if (!map.readable()) return false;
        }

        frame.node          = map;
        frame.parent_frame  = scope_frame_;
        frame.parent_is_map = (scope_kind_ == ScopeKind::Map);
        frame.expecting_key = true;
        frame.use_index_key = false;
        frame.pending_index = 0;
        frame.pending_key.clear();

        scope_kind_  = ScopeKind::Map;
        scope_frame_ = &frame;
        current_     = map;

        if (frame.parent_frame == nullptr) {
            root_ = map;
        }

        return true;
    }

    bool advance_after_value(ArrayFrame&) {
        return ensure_ok();
    }

    bool advance_after_value(MapFrame&) {
        return ensure_ok();
    }

    bool move_to_value(MapFrame& frame) {
        if (!ensure_ok()) return false;
        if (scope_kind_ != ScopeKind::Map || scope_frame_ != &frame) {
            return fail(Error::InvalidState);
        }
        if (frame.expecting_key) {
            return fail(Error::InvalidState);
        }
        return true;
    }

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

        restore_parent_scope(frame.parent_frame, frame.parent_is_map);
        return true;
    }

    bool write_map_end(MapFrame& frame) {
        if (!ensure_ok()) return false;
        if (scope_kind_ != ScopeKind::Map || scope_frame_ != &frame) {
            return fail(Error::InvalidState);
        }

        if (!frame.expecting_key) {
            return fail(Error::InvalidState);
        }

        restore_parent_scope(frame.parent_frame, frame.parent_is_map);
        return true;
    }

    // ---- Primitives ----

    bool write_null() {
        if (!ensure_ok()) return false;
        return attach_scalar_to_current("~");
    }

    bool write_bool(bool const& b) {
        if (!ensure_ok()) return false;
        return attach_scalar_to_current(b ? "true" : "false");
    }

    template<class NumberT>
    bool write_number(NumberT const& value) {
        if (!ensure_ok()) return false;

        char buf[64];
        char* end = nullptr;

        if constexpr (std::is_floating_point_v<NumberT>) {
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<double>(value));
            if (ec != std::errc()) return fail(Error::InvalidState);
            end = ptr;
        } else if constexpr (std::is_signed_v<NumberT>) {
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(value));
            if (ec != std::errc()) return fail(Error::InvalidState);
            end = ptr;
        } else {
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<uint64_t>(value));
            if (ec != std::errc()) return fail(Error::InvalidState);
            end = ptr;
        }

        return attach_scalar_to_current(std::string_view(buf, end - buf));
    }

    // Chunked string writing
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

        bool ok = attach_scalar_to_current(string_buffer_);
        string_buffer_.clear();
        return ok;
    }

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

        return attach_scalar_to_current(std::string_view(data, size));
    }

    // ---- Finish ----

    std::size_t finish() {
        if (!ensure_ok()) return static_cast<std::size_t>(-1);
        if (!tree_) {
            fail(Error::InvalidState);
            return static_cast<std::size_t>(-1);
        }

        if (!root_.readable()) {
            // Empty document - create null root
            tree_->rootref() << "~";
            root_ = tree_->rootref();
        }

        if (!output_sink_finisher_) {
            fail(Error::InvalidState);
            return static_cast<std::size_t>(-1);
        }

        std::size_t bytesWritten = output_sink_finisher_(tree_);
        if (bytesWritten == static_cast<std::size_t>(-1)) {
            fail(Error::InvalidState);
            return static_cast<std::size_t>(-1);
        }

        return bytesWritten;
    }

    // WireSink cleanup callback
    static void ryml_tree_cleanup(char* data, std::size_t size) {
        if (size == sizeof(ryml::Tree*) + sizeof(ryml::id_type)) {
            ryml::Tree* tree = nullptr;
            std::memcpy(&tree, data, sizeof(tree));
            if (tree) {
                delete tree;
            }
        }
    }

    // Create writer from WireSink
    template<WireSinkLike Sink>
    static RapidYamlWriter from_sink(Sink& sink) {
        RapidYamlWriter writer;
        writer.owns_tree_ = false;  // Ownership transferred to sink

        writer.output_sink_finisher_ = [&sink](ryml::Tree* tree) -> std::size_t {
            sink.clear();

            ryml::NodeRef root = tree->rootref();
            if (!root.readable()) {
                return static_cast<std::size_t>(-1);
            }

            constexpr std::size_t needed = sizeof(ryml::Tree*) + sizeof(ryml::id_type);
            if (needed > sink.max_size()) {
                return static_cast<std::size_t>(-1);
            }

            const char* tree_bytes = reinterpret_cast<const char*>(&tree);
            if (!sink.write(tree_bytes, sizeof(ryml::Tree*))) {
                return static_cast<std::size_t>(-1);
            }

            ryml::id_type id = root.id();
            const char* id_bytes = reinterpret_cast<const char*>(&id);
            if (!sink.write(id_bytes, sizeof(ryml::id_type))) {
                return static_cast<std::size_t>(-1);
            }

            sink.set_cleanup(ryml_tree_cleanup);

            return needed;
        };

        return writer;
    }

    // Output from WireSink
    template<WireSinkLike Sink>
    bool output_from_sink(const Sink& sink) {
        if (!ensure_ok()) return false;

        struct CaptureData {
            const ryml::Tree* tree;
            ryml::id_type node_id;
        };

        ryml::ConstNodeRef source_node;

        if (sink.current_size() == sizeof(CaptureData)) {
            // From RapidYamlReader::capture_to_sink
            CaptureData data;
            std::memcpy(&data, sink.data(), sizeof(CaptureData));
            if (data.tree) {
                source_node = data.tree->cref(data.node_id);
            }
        } else if (sink.current_size() == sizeof(ryml::Tree*) + sizeof(ryml::id_type)) {
            // From RapidYamlWriter::from_sink
            ryml::Tree* tree = nullptr;
            ryml::id_type id;
            std::memcpy(&tree, sink.data(), sizeof(ryml::Tree*));
            std::memcpy(&id, sink.data() + sizeof(ryml::Tree*), sizeof(ryml::id_type));
            if (tree) {
                source_node = tree->cref(id);
            }
        } else {
            return fail(Error::InvalidState);
        }

        if (!source_node.readable()) {
            return fail(Error::InvalidState);
        }

        // Deep copy the node into our tree
        ryml::NodeRef dest = attach_node_for_copy();
        if (!dest.readable()) {
            return fail(Error::AllocFailed);
        }

        // Use ryml's built-in deep copy
        dest.tree()->merge_with(source_node.tree(), source_node.id(), dest.id());
        current_ = dest;

        return true;
    }

private:
    enum class ScopeKind { Root, Array, Map };

    ryml::Tree*     tree_;
    ryml::NodeRef   root_;
    ryml::NodeRef   current_;
    error_type      error_;

    ScopeKind scope_kind_;
    void*     scope_frame_;

    std::string string_buffer_;

    bool owns_tree_;
    std::function<std::size_t(ryml::Tree*)> output_sink_finisher_;

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

    ryml::NodeRef attach_container_to_current(ryml::NodeType_e type) {
        switch (scope_kind_) {
        case ScopeKind::Root: {
            ryml::NodeRef node = tree_->rootref();
            node |= type;
            return node;
        }

        case ScopeKind::Array: {
            auto* frame = static_cast<ArrayFrame*>(scope_frame_);
            if (!frame || !frame->node.readable()) {
                fail(Error::InvalidState);
                return ryml::NodeRef{};
            }
            ryml::NodeRef child = frame->node.append_child();
            child |= type;
            return child;
        }

        case ScopeKind::Map: {
            auto* frame = static_cast<MapFrame*>(scope_frame_);
            if (!frame || !frame->node.readable()) {
                fail(Error::InvalidState);
                return ryml::NodeRef{};
            }
            if (frame->expecting_key) {
                fail(Error::InvalidState);
                return ryml::NodeRef{};
            }

            ryml::NodeRef child = frame->node.append_child();
            child |= type;

            // Set the key
            if (frame->use_index_key) {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), frame->pending_index);
                if (ec != std::errc()) {
                    fail(Error::InvalidState);
                    return ryml::NodeRef{};
                }
                child.set_key_serialized(std::string_view(buf, ptr - buf));
                frame->use_index_key = false;
                frame->pending_index = 0;
            } else {
                child.set_key_serialized(frame->pending_key);
                frame->pending_key.clear();
            }

            frame->expecting_key = true;
            return child;
        }
        }

        return ryml::NodeRef{};
    }

    bool attach_scalar_to_current(std::string_view val) {
        switch (scope_kind_) {
        case ScopeKind::Root: {
            tree_->rootref() << val;
            root_ = tree_->rootref();
            current_ = root_;
            return true;
        }

        case ScopeKind::Array: {
            auto* frame = static_cast<ArrayFrame*>(scope_frame_);
            if (!frame || !frame->node.readable()) return fail(Error::InvalidState);
            ryml::NodeRef child = frame->node.append_child();
            child << val;
            current_ = child;
            return true;
        }

        case ScopeKind::Map: {
            auto* frame = static_cast<MapFrame*>(scope_frame_);
            if (!frame || !frame->node.readable()) return fail(Error::InvalidState);
            if (frame->expecting_key) {
                return fail(Error::InvalidState);
            }

            ryml::NodeRef child = frame->node.append_child();
            child |= ryml::KEYVAL;

            // Set the key
            if (frame->use_index_key) {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), frame->pending_index);
                if (ec != std::errc()) return fail(Error::InvalidState);
                child.set_key_serialized(std::string_view(buf, ptr - buf));
                frame->use_index_key = false;
                frame->pending_index = 0;
            } else {
                child.set_key_serialized(frame->pending_key);
                frame->pending_key.clear();
            }

            // Set the value
            child << val;

            frame->expecting_key = true;
            current_ = child;
            return true;
        }
        }

        return false;
    }

    ryml::NodeRef attach_node_for_copy() {
        switch (scope_kind_) {
        case ScopeKind::Root: {
            return tree_->rootref();
        }

        case ScopeKind::Array: {
            auto* frame = static_cast<ArrayFrame*>(scope_frame_);
            if (!frame || !frame->node.readable()) {
                fail(Error::InvalidState);
                return ryml::NodeRef{};
            }
            return frame->node.append_child();
        }

        case ScopeKind::Map: {
            auto* frame = static_cast<MapFrame*>(scope_frame_);
            if (!frame || !frame->node.readable()) {
                fail(Error::InvalidState);
                return ryml::NodeRef{};
            }
            if (frame->expecting_key) {
                fail(Error::InvalidState);
                return ryml::NodeRef{};
            }

            ryml::NodeRef child = frame->node.append_child();

            // Set the key
            if (frame->use_index_key) {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), frame->pending_index);
                if (ec != std::errc()) {
                    fail(Error::InvalidState);
                    return ryml::NodeRef{};
                }
                child.set_key_serialized(std::string_view(buf, ptr - buf));
                frame->use_index_key = false;
                frame->pending_index = 0;
            } else {
                child.set_key_serialized(frame->pending_key);
                frame->pending_key.clear();
            }

            frame->expecting_key = true;
            return child;
        }
        }

        return ryml::NodeRef{};
    }
};

static_assert(writer::WriterLike<RapidYamlWriter>);

} // namespace JsonFusion

#endif // __has_include(<rapidyaml.hpp>)
