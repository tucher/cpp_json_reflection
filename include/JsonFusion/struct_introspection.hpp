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
    
    // Helper consteval functions to get members
    static consteval auto get_members() {
        return std::meta::nonstatic_data_members_of(
            ^^StructT, 
            std::meta::access_context::current()
        );
    }
    
    template<std::size_t Index>
    static consteval auto get_member() {
        auto members = get_members();
        return members[Index];
    }
    
    static consteval std::size_t get_count() {
        return get_members().size();
    }
    
    // Extract OptionsPack<...> annotation from a member (if present)
    template<std::size_t Index>
    static consteval auto get_options_pack_type() {
        constexpr auto member = get_member<Index>();
        auto annots = std::meta::annotations_of(member);
        
        for (auto annot : annots) {
            auto annot_type = std::meta::type_of(annot);
            
            // Check if this is an OptionsPack instantiation
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
        constexpr auto member = get_member<Index>();
        return (s.[:member:]);
    }
    
    template<std::size_t Index>
    static constexpr const auto& getStructElementByIndex(const StructT& s) {
        constexpr auto member = get_member<Index>();
        return (s.[:member:]);
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
