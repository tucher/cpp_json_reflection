#pragma once
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <optional>
#include <memory>
#include "annotated.hpp"
#include "const_string.hpp"
#include "struct_introspection.hpp"

namespace JsonFusion {


namespace options {



namespace detail {

struct exclude_tag{};
struct key_tag{};
struct numeric_key_tag{};
struct allow_excess_fields_tag{};

// struct binary_fields_search_tag{};
struct description_tag{};

struct as_array_tag {};
struct indexes_as_keys_tag {};
struct skip_tag {};
struct skip_nulls_tag{};
struct flatten_tag {};
}

struct exclude {
    using tag = detail::exclude_tag;
    static constexpr std::string_view to_string() {
        return "exclude";
    }
};

struct skip {
    using tag = detail::skip_tag;
    static constexpr std::string_view to_string() {
        return "skip";
    }
};


template<ConstString Desc>
struct key {
    static_assert(Desc.check(), "[[[ JsonFusion ]]] Jsonkey contains control characters");
    using tag = detail::key_tag;
    static constexpr auto desc = Desc;
    static constexpr std::string_view to_string() {
        return "key";
    }
};

template<std::size_t I>
struct int_key {
    using tag = detail::numeric_key_tag;
    static constexpr std::size_t NumericKey = I;
    static constexpr std::string_view to_string() {
        return "int_key";
    }
};




template<ConstString Desc>
struct description {
    static_assert(Desc.check(), "[[[ JsonFusion ]]] key contains control characters");
    using tag = detail::description_tag;
    static constexpr auto desc = Desc;
    static constexpr std::string_view to_string() {
        return "description";
    }
};


struct as_array {
    using tag = detail::as_array_tag;
    static constexpr std::string_view to_string() {
        return "as_array";
    }
};

struct indexes_as_keys {
    using tag = detail::indexes_as_keys_tag;
    static constexpr std::string_view to_string() {
        return "indexes_as_keys";
    }
};

struct skip_nulls {
    using tag = detail::skip_nulls_tag;
    static constexpr std::string_view to_string() {
        return "indexes_as_keys";
    }
};

struct flatten {
    using tag = detail::flatten_tag;
    static constexpr std::string_view to_string() {
        return "flatten";
    }
};

struct allow_excess_fields{
    using tag = detail::allow_excess_fields_tag;
    static constexpr std::string_view to_string() {
        return "allow_excess_fields";
    }
};

namespace detail {


template<class Opt, class Tag, class = void>
struct option_matches_tag : std::false_type {};

template<class Opt, class Tag>
struct option_matches_tag<Opt, Tag, std::void_t<typename Opt::tag>>
    : std::bool_constant<std::is_same_v<typename Opt::tag, Tag>> {};


// === find_option_by_tag ===

template<class Tag, class... Opts>
struct find_option_by_tag;

// Base case: no options → void
template<class Tag>
struct find_option_by_tag<Tag> {
    using type = void;
};

// Recursive case: check First::tag (if present), otherwise continue
template<class Tag, class First, class... Rest>
struct find_option_by_tag<Tag, First, Rest...> {
private:
    using next = typename find_option_by_tag<Tag, Rest...>::type;

public:
    using type = std::conditional_t<
        option_matches_tag<First, Tag>::value,
        First,
        next
        >;
};

struct no_options {
    template<class Tag>
    static constexpr bool has_option = false;

    template<class Tag>
    using get_option = void;
};

template<class OptPack> struct field_options;
template<class... Opts>
struct field_options<OptionsPack<Opts...>> {

    template<class Tag>
    using option_type = typename detail::find_option_by_tag<Tag, Opts...>::type;

    template<class Tag>
    static constexpr bool has_option = !std::is_void_v<option_type<Tag>>;

    template<class Tag>
    using get_option = option_type<Tag>;

};



template<class T>
struct is_options_pack : std::false_type {};

template<class... Opts>
struct is_options_pack<OptionsPack<Opts...>> : std::true_type {};

template<class T>
inline constexpr bool is_options_pack_v = is_options_pack<T>::value;


template<class T, class = void>
struct has_annotation_specialization_impl : std::false_type {};

template<class T>
struct has_annotation_specialization_impl<T,
                                          std::void_t<typename Annotated<T>::Options>
                                          > : std::bool_constant<
                                                                 is_options_pack_v<typename Annotated<T>::Options>
                                                                    && (Annotated<T>::Options::Count > 0)
                                                                 > {};

template<class T>
inline constexpr bool has_annotation_specialization =
    has_annotation_specialization_impl<T>::value;

template<class Field>
struct annotation_meta{};

// Base: non-annotated, non-optional
template<class T>
    requires (!has_annotation_specialization<T>)
struct annotation_meta<T> {
    using value_t = T;
    using options      = no_options;
    using OptionsP = OptionsPack<>;
    static constexpr decltype(auto) getRef(T & f) {
        return (f);
    }
    static constexpr decltype(auto) getRef(const T & f) {
        return (f);
    }
};

// Optional, non-annotated
template<class T, class... Opts>
struct annotation_meta<std::optional<Annotated<T, Opts...>>> {
    static_assert(!sizeof(T), "[[[ JsonFusion ]]] Use Annotated<std::optional<T>, ...> instead of std::optional<Annotated<T, ...>>");
};

template<class T, class... Opts>
struct annotation_meta<std::unique_ptr<Annotated<T, Opts...>>> {
    static_assert(!sizeof(T), "[[[ JsonFusion ]]] Use Annotated<std::unique_ptr<T>, ...> instead of std::unique_ptr<Annotated<T, ...>>");
};


template <class P1, class P2> struct merge_options;
template <class ... Opts1, class ... Opts2> struct merge_options<OptionsPack<Opts1...>, OptionsPack<Opts2...>> {
    using type = OptionsPack<Opts1..., Opts2...>;
};


// Annotated<T, Opts...>
template<class T, class... Opts>
struct annotation_meta<Annotated<T, Opts...>> {
    // static_assert((requires { typename Opts::tag; } && ...));

    using OptionsP = OptionsPack<Opts...>;

    using value_t = T;
    // using external_opts =
    //     std::conditional_t<has_annotation_specialization<T>,
    //                        typename Annotated<T>::Options,
    //                        OptionsPack<>>;
    // using options      = field_options< // Concat them??
    //     typename merge_options<external_opts, OptionsPack<Opts...>>::type
    // >;
    using options      = field_options<
        OptionsPack<Opts...>
        >;

    static constexpr decltype(auto) getRef(Annotated<T, Opts...> & f) {
        return (f.value);
    }
    static constexpr decltype(auto) getRef(const Annotated<T, Opts...> & f) {
        return (f.value);
    }

    //case for externally injected annotations:  pure object ref is passed there, but still this specialization is used, because there is virtual annotation
    static constexpr decltype(auto) getRef(T & f) {
        return (f);
    }
    static constexpr decltype(auto) getRef(const T & f) {
        return (f);
    }
};



// Externally Annotated<T>
template<class T> requires has_annotation_specialization<T>
struct annotation_meta<T> {
    // static_assert((requires { typename Opts::tag; } && ...));
    using value_t = T;
    using options      = field_options<typename Annotated<T>::Options>;

    using OptionsP = Annotated<T>::Options;

    static constexpr decltype(auto) getRef(T & f) {
        return (f);
    }
    static constexpr decltype(auto) getRef(const T & f) {
        return (f);
    }

};

// Entry point with decay
template<class Field>
struct annotation_meta_getter : annotation_meta<std::remove_cvref_t<Field>> {};



template<class T, std::size_t I, class = void>
struct has_field_annotation_specialization_impl : std::false_type {
    using Options = OptionsPack<>;
};

template<class T, std::size_t I>
struct has_field_annotation_specialization_impl<T, I,
                                          std::void_t<typename AnnotatedField<T, I>::Options>
                                          > : std::bool_constant<
                                                  is_options_pack_v<typename AnnotatedField<T, I>::Options>
                                                  && (AnnotatedField<T, I>::Options::Count > 0)
                                                  > {
    using Options = AnnotatedField<T, I>::Options;
};

template<class AggregateT, std::size_t Index>
struct aggregate_field_opts {
    using Field   = introspection::structureElementTypeByIndex<Index, AggregateT>;
    using Meta = annotation_meta_getter<Field>;
    using ExternalOpts = typename has_field_annotation_specialization_impl<AggregateT, Index>::Options;
    using options      = field_options<
        typename merge_options<ExternalOpts, typename Meta::OptionsP>::type
    >;
};

template<class AggregateT, std::size_t Index>
using aggregate_field_opts_getter =  aggregate_field_opts<std::remove_cvref_t<AggregateT>, Index>::options;






} // namespace detail


} //namespace options


} // namespace JsonFusion
