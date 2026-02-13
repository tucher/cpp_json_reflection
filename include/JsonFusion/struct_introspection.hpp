#pragma once

#include <cstddef>
#include <string_view>

// Feature detection: Check if C++26 reflection is available
// GCC uses __cpp_impl_reflection for experimental reflection support
#if defined(__cpp_impl_reflection) || \
    (defined(__cpp_reflection) && __cpp_reflection >= 202306L)
    #define JSONFUSION_USE_REFLECTION 1
    #include <meta>
#else
    #define JSONFUSION_USE_REFLECTION 0
#endif

// Include PFR only when not using reflection
#if !JSONFUSION_USE_REFLECTION
    #if __has_include(<boost/pfr.hpp>)
        #include <boost/pfr/tuple_size.hpp>
        #include <boost/pfr/core.hpp>
        #include <boost/pfr/core_name.hpp>
        using namespace boost;
    #else 
        #include <pfr/tuple_size.hpp>
        #include <pfr/core.hpp>
        #include <pfr/core_name.hpp>
    #endif
#endif

// JsonFusion-specific includes (only if not already provided)
#ifndef JSONFUSION_CONST_STRING_HPP
#include "const_string.hpp"
#endif
#ifndef JSONFUSION_ANNOTATED_HPP
#include "annotated.hpp"
#endif

namespace JsonFusion {

// Forward declaration for flatten option (defined in options.hpp)
namespace options { struct flatten; }

template <class ... Flds>
struct StructMeta {

};

template <auto MPtr, ConstString key, class ... Opts>
struct Field;

template <typename C, typename T, T C::*MPtr, ConstString key, class ... Opts>
struct Field<MPtr, key, Opts...>{
    using ClassT = C;
    using ValueT = T;
    using OptionsP = OptionsPack<Opts...>;
    static constexpr ConstString Name  = key;
    static constexpr  T C::* MemberP = MPtr;
};

template <class ... F>
struct StructFields{
    using FieldsTuple = std::tuple<F...>;
};

namespace introspection {

namespace detail {

#if JSONFUSION_USE_REFLECTION
// ============================================================================
// C++26 REFLECTION-BASED IMPLEMENTATION (No external dependencies!)
// ============================================================================

// Trait to detect Annotated<T, ...>
template<class T> struct is_annotated : std::false_type {};
template<class U, class... Opts> struct is_annotated<Annotated<U, Opts...>> : std::true_type {};
template<class T> inline constexpr bool is_annotated_v = is_annotated<T>::value;

// Helper to convert OptionsPack to Annotated (handles const-qualified packs from annotations)
// If T is already Annotated<...>, don't wrap it again
template <class T, class OptPack, bool IsAlreadyAnnotated = is_annotated_v<T>> 
struct AnnotationFillerFromPack;

// Normal case: wrap T in Annotated<T, Opts...>
template <class T, class ...Opts> 
struct AnnotationFillerFromPack<T, OptionsPack<Opts...>, false> {
    using type = Annotated<T, Opts...>;
};

template <class T, class ...Opts> 
struct AnnotationFillerFromPack<T, const OptionsPack<Opts...>, false> {
    using type = Annotated<T, Opts...>;
};

// Already Annotated: pass through unchanged (don't double-wrap)
template <class T, class ...Opts> 
struct AnnotationFillerFromPack<T, OptionsPack<Opts...>, true> {
    using type = T;  // Already Annotated<U, ...>, use as-is
};

template <class T, class ...Opts> 
struct AnnotationFillerFromPack<T, const OptionsPack<Opts...>, true> {
    using type = T;  // Already Annotated<U, ...>, use as-is
};

template<class T>
struct IntrospectionImpl {
    using StructT = std::remove_cv_t<T>;
    
    // Recursively collect nonstatic data members from a type and all its bases.
    // Base class members come first (depth-first, left-to-right), then own members.
    static consteval std::vector<std::meta::info> collect_members_recursive(std::meta::info type_refl) {
        std::vector<std::meta::info> result;
        
        // First, collect from base classes (depth-first, left-to-right)
        auto bases = std::meta::bases_of(type_refl, std::meta::access_context::current());
        for (auto base : bases) {
            auto base_members = collect_members_recursive(std::meta::type_of(base));
            for (auto m : base_members) {
                result.push_back(m);
            }
        }
        
        // Then, append own direct members
        auto own = std::meta::nonstatic_data_members_of(type_refl, std::meta::access_context::current());
        for (auto m : own) {
            result.push_back(m);
        }
        
        return result;
    }
    
    // Deduplicate members by name, keeping the last occurrence (derived wins over base).
    static consteval std::vector<std::meta::info> deduplicate_members(std::vector<std::meta::info> members) {
        std::vector<std::meta::info> result;
        for (std::size_t i = 0; i < members.size(); ++i) {
            bool overridden = false;
            for (std::size_t j = i + 1; j < members.size(); ++j) {
                if (std::meta::identifier_of(members[i]) == std::meta::identifier_of(members[j])) {
                    overridden = true;
                    break;
                }
            }
            if (!overridden) {
                result.push_back(members[i]);
            }
        }
        return result;
    }
    
    // Raw members: base classes + own, deduplicated (before flatten expansion)
    static consteval std::vector<std::meta::info> get_raw_members() {
        return deduplicate_members(collect_members_recursive(^^StructT));
    }
    
    // Check if a member's declared type is Annotated<T, ..., flatten, ...>
    static consteval bool has_flatten_in_type(std::meta::info member) {
        auto member_type = std::meta::type_of(member);
        if (std::meta::has_template_arguments(member_type)) {
            auto tmpl = std::meta::template_of(member_type);
            if (tmpl == ^^Annotated) {
                auto args = std::meta::template_arguments_of(member_type);
                for (auto arg : args) {
                    if (arg == ^^options::flatten) {
                        return true;
                    }
                }
            }
        }
        return false;
    }
    
    // Check if a member has flatten — either via C++26 annotation or wrapper type
    static consteval bool has_flatten_annotation(std::meta::info member) {
        // Check C++26 annotation: [[=A<options::flatten>{}]]
        auto annots = std::meta::annotations_of(member);
        for (auto annot : annots) {
            auto annot_type = std::meta::type_of(annot);
            if (std::meta::has_template_arguments(annot_type)) {
                auto tmpl = std::meta::template_of(annot_type);
                if (tmpl == ^^Annotated) {
                    auto args = std::meta::template_arguments_of(annot_type);
                    for (auto arg : args) {
                        if (arg == ^^options::flatten) {
                            return true;
                        }
                    }
                }
            }
        }
        // Check wrapper type: A<T, flatten> / Annotated<T, flatten>
        return has_flatten_in_type(member);
    }
    
    // Get the inner type for flatten expansion:
    // - C++26 annotation on plain type: member's declared type IS the inner type
    // - Wrapper type Annotated<T, flatten>: T (first template arg) is the inner type
    static consteval std::meta::info get_flatten_inner_type(std::meta::info member) {
        if (has_flatten_in_type(member)) {
            auto member_type = std::meta::type_of(member);
            auto args = std::meta::template_arguments_of(member_type);
            return args[0]; // T in Annotated<T, flatten>
        }
        return std::meta::type_of(member);
    }
    
    // Get the 'value' data member of an Annotated<T, ...> type (always its first member)
    static consteval std::meta::info get_annotated_value_member(std::meta::info annotated_type) {
        auto members = std::meta::nonstatic_data_members_of(
            annotated_type, std::meta::access_context::current()
        );
        return members[0]; // Annotated<T, ...>::value
    }
    
    // Expanded member list: flatten-annotated fields are replaced by their
    // inner type's members. Non-flatten fields pass through unchanged.
    static consteval std::vector<std::meta::info> get_members() {
        std::vector<std::meta::info> result;
        auto raw = get_raw_members();
        for (auto m : raw) {
            if (has_flatten_annotation(m)) {
                auto inner_type = get_flatten_inner_type(m);
                auto inner_members = deduplicate_members(
                    collect_members_recursive(inner_type)
                );
                for (auto im : inner_members) {
                    result.push_back(im);
                }
            } else {
                result.push_back(m);
            }
        }
        return result;
    }
    
    template<std::size_t Index>
    static consteval auto get_member() {
        auto members = get_members();
        return members[Index];
    }
    
    static consteval std::size_t get_count() {
        return get_members().size();
    }
    
    // For a given expanded index, determine if it came from a flatten expansion
    template<std::size_t Index>
    static consteval bool is_through_flatten() {
        auto raw = get_raw_members();
        std::size_t current = 0;
        for (std::size_t ri = 0; ri < raw.size(); ++ri) {
            if (has_flatten_annotation(raw[ri])) {
                auto inner_type = get_flatten_inner_type(raw[ri]);
                auto count = deduplicate_members(
                    collect_members_recursive(inner_type)
                ).size();
                if (Index < current + count) return true;
                current += count;
            } else {
                if (Index == current) return false;
                current++;
            }
        }
        return false;
    }
    
    // For a flatten-expanded index, get the outer "container" member
    template<std::size_t Index>
    static consteval auto get_flatten_container() {
        auto raw = get_raw_members();
        std::size_t current = 0;
        for (std::size_t ri = 0; ri < raw.size(); ++ri) {
            if (has_flatten_annotation(raw[ri])) {
                auto inner_type = get_flatten_inner_type(raw[ri]);
                auto count = deduplicate_members(
                    collect_members_recursive(inner_type)
                ).size();
                if (Index < current + count) return raw[ri];
                current += count;
            } else {
                current++;
            }
        }
        return raw[0]; // unreachable for valid Index
    }
    
    // Extract OptionsPack<...> annotation from a member (if present)
    template<std::size_t Index>
    static consteval auto get_options_pack_type() {
        constexpr auto member = get_member<Index>();
        auto annots = std::meta::annotations_of(member);
        
        for (auto annot : annots) {
            auto annot_type = std::meta::type_of(annot);
            
            // Check if this is an Annotated instantiation
            if (std::meta::has_template_arguments(annot_type)) {
                auto tmpl = std::meta::template_of(annot_type);
                if (tmpl == ^^Annotated) {
                    auto args = std::meta::template_arguments_of(annot_type);
                    return std::meta::substitute(^^OptionsPack, args);
                }
            }
        }
        
        return ^^OptionsPack<>;  // Default: empty OptionsPack
    }
    
    // Public interface
    static constexpr std::size_t structureElementsCount = get_count();
    
    template<std::size_t Index>
    static constexpr auto& getStructElementByIndex(StructT& s) {
        if constexpr (is_through_flatten<Index>()) {
            constexpr auto container = get_flatten_container<Index>();
            constexpr auto member = get_member<Index>();
            if constexpr (has_flatten_in_type(container)) {
                // Wrapper type: A<T, flatten> — go through .value
                constexpr auto value_m = get_annotated_value_member(std::meta::type_of(container));
                return s.[:container:].[:value_m:].[:member:];
            } else {
                // C++26 annotation: direct access
                return s.[:container:].[:member:];
            }
        } else {
            constexpr auto member = get_member<Index>();
            return (s.[:member:]);
        }
    }
    
    template<std::size_t Index>
    static constexpr const auto& getStructElementByIndex(const StructT& s) {
        if constexpr (is_through_flatten<Index>()) {
            constexpr auto container = get_flatten_container<Index>();
            constexpr auto member = get_member<Index>();
            if constexpr (has_flatten_in_type(container)) {
                // Wrapper type: A<T, flatten> — go through .value
                constexpr auto value_m = get_annotated_value_member(std::meta::type_of(container));
                return s.[:container:].[:value_m:].[:member:];
            } else {
                // C++26 annotation: direct access
                return s.[:container:].[:member:];
            }
        } else {
            constexpr auto member = get_member<Index>();
            return (s.[:member:]);
        }
    }
    
    // Returns Annotated<T, Opts...> where Opts are extracted from [[=OptionsPack<...>{}]] annotation
    template<std::size_t Index>
    using structureElementTypeByIndex =
        typename AnnotationFillerFromPack<
            typename [: std::meta::type_of(get_member<Index>()) :],
            typename [: get_options_pack_type<Index>() :]
        >::type;
    
    template<std::size_t Index>
    static constexpr std::string_view structureElementNameByIndex = 
        std::meta::identifier_of(get_member<Index>());
};

#else
// ============================================================================
// BOOST.PFR-BASED IMPLEMENTATION (C++20/C++23 fallback)
// ============================================================================

template<class T>
struct IntrospectionImpl {
    using StructT = std::remove_cv_t<T>;

    template<std::size_t Index>
    static constexpr decltype(auto) getStructElementByIndex(StructT & s) {
        return (pfr::get<Index>(s));
    }

    template<std::size_t Index>
    static constexpr decltype(auto) getStructElementByIndex(const StructT & s) {
        return (pfr::get<Index>(s));
    }

    static constexpr std::size_t structureElementsCount = pfr::tuple_size_v<StructT>;

    template<std::size_t Index>
    using structureElementTypeByIndex = pfr::tuple_element_t<Index, StructT>;

    template<std::size_t Index>
    static constexpr std::string_view structureElementNameByIndex = pfr::get_name<Index, StructT>();
};

#endif // JSONFUSION_USE_REFLECTION

// ============================================================================
// COMMON IMPLEMENTATION FOR CUSTOM StructMeta (both reflection and PFR)
// ============================================================================

template<class T>
struct is_fields_pack : std::false_type {};

template<class... Field>
struct is_fields_pack<StructFields<Field...>> : std::true_type {};

template<class T>
inline constexpr bool is_fields_pack_v = is_fields_pack<T>::value;

template<class T, class = void>
struct has_struct_meta_specialization_impl : std::false_type {};

template<class T>
struct has_struct_meta_specialization_impl<T,
                                          std::void_t<typename StructMeta<T>::Fields>
                                          > : std::bool_constant<
                                                  is_fields_pack_v<typename StructMeta<T>::Fields>
                                                  > {};

template<class T>
inline constexpr bool has_struct_meta_specialization =
    has_struct_meta_specialization_impl<T>::value;

template <class T, class OptPack> struct AnnotationFiller;
template <class T, class ...Opts> struct AnnotationFiller<T, OptionsPack<Opts...>> {
    using type = Annotated<T, Opts...>;
};

// Custom StructMeta specialization (works with both reflection and PFR)
template <class T>
requires (has_struct_meta_specialization<T>)
struct IntrospectionImpl<T> {
    using Fields = typename StructMeta<T>::Fields::FieldsTuple;
    static constexpr std::size_t structureElementsCount = std::tuple_size_v<Fields>;

    template<std::size_t Index, class StructT>
    static constexpr decltype(auto) getStructElementByIndex(StructT & s) {
        using Field = std::tuple_element_t<Index, Fields>;
        return (s.*(Field::MemberP));
    }

    template<std::size_t Index, class StructT>
    static constexpr decltype(auto) getStructElementByIndex(const StructT & s) {
        using Field = std::tuple_element_t<Index, Fields>;
        return (s.*(Field::MemberP));
    }

    template<std::size_t Index>
    using structureElementTypeByIndex = AnnotationFiller<
                                        typename std::tuple_element_t<Index, Fields>::ValueT,
                                        typename std::tuple_element_t<Index, Fields>::OptionsP
                                        >::type;

    template<std::size_t Index>
    static constexpr std::string_view structureElementNameByIndex =
        std::tuple_element_t<Index, Fields>::Name.toStringView();
};

} // namespace detail

// ============================================================================
// PUBLIC API (identical for both implementations)
// ============================================================================

template<std::size_t Index, class StructT>
constexpr decltype(auto) getStructElementByIndex(StructT & s) {
    using Impl = detail::IntrospectionImpl<std::remove_cv_t<StructT>>;
    return (Impl::template getStructElementByIndex<Index>(s));
}

template<std::size_t Index, class StructT>
constexpr decltype(auto) getStructElementByIndex(const StructT & s) {
    using Impl = detail::IntrospectionImpl<std::remove_cv_t<StructT>>;
    return (Impl::template getStructElementByIndex<Index>(
        const_cast<std::remove_cv_t<StructT>&>(s)
        ));
}

template<class StructT>
static constexpr std::size_t structureElementsCount = 
    detail::IntrospectionImpl<std::remove_cv_t<StructT>>::structureElementsCount;

template<std::size_t Index, class StructT>
using structureElementTypeByIndex = 
    detail::IntrospectionImpl<std::remove_cv_t<StructT>>::template structureElementTypeByIndex<Index>;

template<std::size_t Index, class StructT>
static constexpr std::string_view structureElementNameByIndex = 
    detail::IntrospectionImpl<std::remove_cv_t<StructT>>::template structureElementNameByIndex<Index>;

// ============================================================================
// HELPER FOR MEMBER POINTER INDEX LOOKUP
// ============================================================================

namespace detail {

constexpr std::size_t NOT_A_MEMBER = static_cast<std::size_t>(-1);

template<class T, auto MemberPtr, std::size_t... Is>
consteval std::size_t index_for_member_ptr_impl(std::index_sequence<Is...>) {
    T obj{};
    auto& target = obj.*MemberPtr;
    std::size_t result = NOT_A_MEMBER;

    (
        [&]{
            if (&target == &getStructElementByIndex<Is>(obj)) {
                result = Is;
            }
        }(),
        ...
    );

    return result;
}

template<class T, auto MemberPtr>
consteval std::size_t index_for_member_ptr() {
    constexpr std::size_t n = structureElementsCount<T>;
    return index_for_member_ptr_impl<T, MemberPtr>(std::make_index_sequence<n>{});
}

} // namespace detail

} // namespace introspection
} // namespace JsonFusion

// ============================================================================
// COMPILE-TIME DIAGNOSTICS
// ============================================================================

// #if JSONFUSION_USE_REFLECTION
//     #pragma message("JsonFusion: Using C++26 reflection (no external dependencies)")
// #else
//     #pragma message("JsonFusion: Using Boost.PFR/PFR (C++20/23 mode)")
// #endif
