#ifndef CPP_JSON_REFLECTION_HPP
#define CPP_JSON_REFLECTION_HPP

#include <pfr.hpp>
#include <type_traits>
#include <concepts>
#include <cmath>

#include <inttypes.h>
#include <iterator>
#include <swl/variant.hpp>
#include <algorithm>
#include <fast_double_parser.h>
#include <simdjson/to_chars.hpp>
#include "string_ops.hpp"


namespace JSONReflection {

namespace  d {
constexpr std::uint8_t SkippingMaxNestingLevel = 32;
using SerializerStubT = bool (*)(const char *data, std::size_t size);
static_assert (SerializerOutputCallbackConcept<SerializerStubT>);

template <typename CharT, std::size_t N> struct ConstString
{
    constexpr bool check()  const {
        for(int i = 0; i < N; i ++) {
            if(std::uint8_t(m_data[i]) < 32) return false;
        }
        return true;
    }
    constexpr ConstString(const CharT (&foo)[N+1]) {
        std::copy_n(foo, N+1, m_data);
    }
    CharT m_data[N+1];
    static constexpr std::size_t Length = N;
    static constexpr std::size_t CharSize = sizeof (CharT);
    constexpr std::string_view toStringView() const {
        return {&m_data[0], &m_data[Length]};
    }
};
template <typename CharT, std::size_t N>
ConstString(const CharT (&str)[N])->ConstString<CharT, N-1>;


struct JSONValueKindEnumPlain {};
struct JSONValueKindEnumObject {};
struct JSONValueKindEnumArray {};
struct JSONValueKindEnumMap {};

template<typename T>
concept JSONWrappedValueCompatible = requires {
        typename T::JSONValueKind;
        requires std::is_same_v<typename T::JSONValueKind, JSONValueKindEnumPlain>
        || std::is_same_v<typename T::JSONValueKind, JSONValueKindEnumObject>
        || std::is_same_v<typename T::JSONValueKind, JSONValueKindEnumArray>
        || std::is_same_v<typename T::JSONValueKind, JSONValueKindEnumMap>;
        };

template <typename T>
concept StringTypeConcept = requires (T v) {
    typename T::value_type;
    requires std::is_pointer_v<decltype(v.data())>;
    requires std::is_integral_v<std::remove_pointer_t<decltype(v.data())>>;
    {v.size()} -> std::convertible_to<std::size_t>;
    {v[0]} -> std::convertible_to<typename T::value_type>;
    requires !JSONWrappedValueCompatible<typename T::value_type>;
};

template <typename T>
concept StaticContainerTypeConcept = std::ranges::sized_range<T>;



template <typename T>
concept DynamicStringTypeConcept = StringTypeConcept<T> && DynamicContainerTypeConcept<T>;

template<typename T>
concept CustomMappable =
        !JSONWrappedValueCompatible<T>
        && requires (T val, const T&constVal) {
    typename T::DeserializeContainerT;
    requires StringOutputContainerConcept<typename T::DeserializeContainerT>;
    {constVal.JSONSerialize(std::declval<SerializerStubT>())} -> std::same_as<bool>; //??
    {val.JSONDeserialise(std::declval<typename T::DeserializeContainerT &>())} -> std::same_as<bool>; //??
};

template<typename T>
concept JSONBasicValue =
        !JSONWrappedValueCompatible<T> && (
        std::same_as<T, bool>
    || std::same_as<T, double>
    || (std::convertible_to<T, std::int64_t> && std::convertible_to<std::int64_t, T>)
    || StringTypeConcept<T>
    || CustomMappable<T>
            );

template<typename T>
concept JSONArrayValue = std::ranges::range<T> && !JSONBasicValue<T> && JSONWrappedValueCompatible<typename T::value_type>;

template<typename T>
concept JSONMapValue = !JSONBasicValue<T> && JSONWrappedValueCompatible<typename T::mapped_type>
        && requires (T val) {
    val["fuuu"] = std::declval<typename T::mapped_type>();
    val.insert({std::declval<typename T::key_type>(), std::declval<typename T::mapped_type>()});
};

template<typename T>
concept JSONObjectValue = std::is_class_v<T> && !JSONArrayValue<T> && !JSONBasicValue<T> && !JSONMapValue<T>;


template<typename T>
concept JSONWrapable = JSONBasicValue<T> || JSONArrayValue<T> || JSONObjectValue<T> || JSONMapValue<T>;

}

template <class Src, d::ConstString Str = "">
class J {
    template <class... T>
    static constexpr bool always_false = false;
    static_assert(always_false<Src>, "JSONReflection: Cannot represent type in terms of JSON. See next compiler messages for details");
    static_assert (d::JSONWrapable<Src>);
};

template <d::JSONBasicValue Src, d::ConstString Str>
class J<Src, Str> {
    Src content;

    //from 3party/simdjson/to_chars.cpp
    char *to_chars(char *first, const char *last, double value) const {
      static_cast<void>(last); // maybe unused - fix warning
      bool negative = std::signbit(value);
      if (negative) {
        value = -value;
        *first++ = '-';
      }

      if (value == 0) // +-0
      {
        *first++ = '0';
        if(negative) {
          *first++ = '.';
          *first++ = '0';
        }
        return first;
      }

      int len = 0;
      int decimal_exponent = 0;
      simdjson::internal::dtoa_impl::grisu2(first, len, decimal_exponent, value);
      // Format the buffer like printf("%.*g", prec, value)
      constexpr int kMinExp = -4;
      constexpr int kMaxExp = std::numeric_limits<double>::digits10;

      return simdjson::internal::dtoa_impl::format_buffer(first, len, decimal_exponent, kMinExp,
                                      kMaxExp);
    }


public:
    using JSONValueKind = d::JSONValueKindEnumPlain;
    static constexpr auto FieldName = Str;
    static_assert(FieldName.check() == true, "Please, use printable chars in values keys");

    operator Src&() {
        return content;
    }
    operator const Src&() const {
        return content;
    }
    constexpr J(): content(Src())  {}
    J(const Src & val):content(val)  {}

    Src & operator=(const Src & other) {
        return content = other;
    }

    auto operator<=>(const J&) const = default;
    auto operator<=>(const Src& rhs) const {
        return content <=> rhs;
    }
    auto operator==(const Src& rhs) const {
        return content == rhs;
    }
    auto operator!=(const Src& rhs) const {
        return content != rhs;
    }

    bool SerializeInternal(SerializerOutputCallbackConcept auto && clb) const {
        if constexpr(std::same_as<bool, Src>) {
            if(content) {
                constexpr char v[] = "true";
                return clb(v, sizeof(v)-1);
            }
            else {
                constexpr char v[] = "false";
                return clb(v, sizeof(v)-1);
            }
        } else if constexpr(d::StringTypeConcept<Src>) {
            if(char v[] = "\""; !clb(v, sizeof(v)-1))  [[unlikely]] {
                return false;
            }
            if constexpr(d::DynamicStringTypeConcept<Src>) {
                if(!d::outputEscapedString(reinterpret_cast<const char*>(content.data()), content.size() * sizeof (typename Src::value_type), std::forward<std::decay_t<decltype(clb)>>(clb)))  [[unlikely]] {
                    return false;
                }
            } else {
                std::size_t zp = 0;
                while(zp < content.size () && content[zp] != 0) zp ++;
                if(!d::outputEscapedString(reinterpret_cast<const char*>(content.data()), zp * sizeof (typename Src::value_type), std::forward<std::decay_t<decltype(clb)>>(clb)))  [[unlikely]] {
                    return false;
                }
            }
            if(char v[] = "\""; !clb(v, sizeof(v)-1)) [[unlikely]] {
                return false;
            }
            return true;
        } else if constexpr(d::CustomMappable<Src>) {
            if(char v[] = "\""; !clb(v, sizeof(v)-1)) [[unlikely]] {
                return false;
            }
            auto wr = [&clb](const char *data, std::size_t size) {
                return d::outputEscapedString(data, size, std::forward<std::decay_t<decltype(clb)>>(clb));
            };
            if(!content.JSONSerialize(std::forward<std::decay_t<decltype(wr)>>(wr)))  [[unlikely]] {
                return false;
            }
            if(char v[] = "\""; !clb(v, sizeof(v)-1)) [[unlikely]] {
                return false;
            }
            return true;
        } else if constexpr(std::same_as<double, Src> || std::same_as<std::int64_t, Src>) {
            if(std::isnan(content) || std::isinf(content)) {
                char v[] = "0";
                return clb(v, 1);
            } else {
                char buf[50];
                char * endChar = to_chars(buf, buf + sizeof (buf), content);
                auto s = endChar-buf;
                if(endChar-buf == sizeof (buf))  [[unlikely]] {
                    return false;
                }
                else {
                    return clb(buf, s);
                }
            }
        } else
            return false;
    }

    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool DeserializeInternal(InpIter & begin, const InpIter & end, DeserializationContext & ctx) {
        if constexpr(std::same_as<bool, Src>) {
            char fc = *begin;
            begin++;
            if(fc == 't' || fc == 'f') [[likely]] {
                constexpr char vt[] = "rue";
                constexpr char vf[] = "alse";
                const char * v;
                std::size_t sz;
                bool to_assign;
                if(fc == 't') {
                    v = vt;
                    to_assign = true;
                    sz = sizeof (vt) - 1;
                }
                else {
                    v = vf;
                    to_assign = false;
                    sz = sizeof (vf) - 1;
                }
                if(end-begin < sz)  [[unlikely]] {
                    ctx.setError(DeserializationContext::UNEXPECTED_END_OF_DATA, end - begin);
                    return false;
                }
                for(int i = 0; i < sz; i ++) {
                    if(*begin != v[i]) [[unlikely]] {
                        ctx.setError(DeserializationContext::UNEXPECTED_SYMBOL, end - begin);
                        return false;
                    }
                    begin ++;
                }
                content = to_assign;
                return true;
            } else {
                ctx.setError(DeserializationContext::UNEXPECTED_SYMBOL, end - begin);
                return false;
            }
        } else if constexpr(d::StringTypeConcept<Src>) {
            if constexpr (d::DynamicContainerTypeConcept<Src>) {
                content.clear();
            }
            return d::extractJSString(begin, end, ctx, content);
        }  else if constexpr(d::CustomMappable<Src>) {
            typename Src::DeserializeContainerT container;
            if(!d::extractJSString(begin, end, ctx, container)) [[unlikely]] {
                return false;
            }
            if(!content.JSONDeserialise(container)) [[unlikely]]{
                ctx.setError(DeserializationContext::CUSTOM_MAPPER_ERROR, end - begin);
            } else {
                return true;
            }
        } else if constexpr(std::same_as<double, Src> || std::same_as<std::int64_t, Src>) {
             char buf[40];
             std::size_t index = 0;
             while(!d::isPlainEnd(*begin)) {
                 if(begin == end ) [[unlikely]] {
                     ctx.setError(DeserializationContext::UNEXPECTED_END_OF_DATA, end - begin);
                     return false;
                 }
                 buf[index] = *begin;
                 begin ++;
                 index ++;
                 if(index > sizeof (buf) - 1) [[unlikely]] {
                     ctx.setError(DeserializationContext::ILLFORMED_NUMBER, end - begin);
                     return false;
                 }
             }
             buf[index] = 0;
             double x;
             if(fast_double_parser::parse_number(buf, &x) != nullptr) {
                 content = x;
                 return true;
             }
             ctx.setError(DeserializationContext::ILLFORMED_NUMBER, end - begin);
             return false;
        }
        ctx.setError(DeserializationContext::INTERNAL_ERROR, end - begin);
        return false;
    }
};

template <d::JSONArrayValue Src, d::ConstString Str>
class J<Src, Str> : public Src{
    using ItemType = typename Src::value_type;
public:
    using JSONValueKind = d::JSONValueKindEnumArray;
    static constexpr auto FieldName = Str;
    static_assert(FieldName.check() == true, "Please, use printable chars in values keys");

    Src& operator = (const Src& other) {
        return static_cast<Src &>(*this) = other;
    }
    J() = default;
    J(const Src & other): Src(other) {}


    bool SerializeInternal(SerializerOutputCallbackConcept auto && clb) const {
        if(char v[] = "["; !clb(v, sizeof(v)-1)) [[unlikely]] {
            return false;
        }
        std::size_t last = static_cast<const Src&>(*this).size()-1;
        std::size_t i = 0;
        for(const auto & item: static_cast<const Src&>(*this)) {
            if(!item.SerializeInternal(std::forward<std::decay_t<decltype(clb)>>(clb)))  [[unlikely]] {
                return false;
            }
            if(i != last) {
                if(char v[] = ","; !clb(v, sizeof(v)-1))  [[unlikely]] {
                    return false;
                }
            }
            i++;
        }
        if(char v[] = "]"; !clb(v, sizeof(v)-1))  [[unlikely]] {
            return false;
        }
        return true;
    }

    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool DeserializeInternal(InpIter & begin, const InpIter & end, DeserializationContext & ctx) {
        if(!d::skipWhiteSpaceTill(begin, end, '[', ctx)) [[unlikely]] {
            return false;
        }
        if constexpr (d::DynamicContainerTypeConcept<Src>) {
            static_cast<Src&>(*this).clear();

            while(begin != end) {
                if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                    return false;
                }
                if(*begin == ']') {
                    begin ++;
                    return true;
                }
                ItemType & newItem = static_cast<Src&>(*this).emplace_back();
                if(end-begin>=4 && *(begin+0) == 'n'&&*(begin+1) == 'u'&&*(begin+2) == 'l'&&*(begin+3) == 'l') {
                    begin += 4;
                } else {
                    if(!newItem.DeserializeInternal(begin, end, ctx)) {
                        return false;
                    }
                }

                if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                    return false;
                }
                if(*begin == ',') {
                    begin ++;
                }
            }
        } else {
            auto containerI = static_cast<Src&>(*this).begin();

            while(begin != end) {
                if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                    return false;
                }
                if(*begin == ']') {
                    if(containerI != static_cast<const Src&>(*this).end()) {
                        ctx.setError(DeserializationContext::FIXED_SIZE_CONTAINER_UNDERFLOW, end - begin);
                        return false;
                    }
                    begin ++;
                    return true;
                }
                if(containerI == static_cast<const Src&>(*this).end()) {
                    ctx.setError(DeserializationContext::FIXED_SIZE_CONTAINER_OVERFLOW, end - begin);
                    return false;
                }
                if(end-begin>=4 && *(begin+0) == 'n'&&*(begin+1) == 'u'&&*(begin+2) == 'l'&&*(begin+3) == 'l') {
                    begin += 4;
                    *containerI = ItemType{};
                } else {
                    if(!(*containerI).DeserializeInternal(begin, end, ctx)) {
                        return false;
                    }
                }

                if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                    return false;
                }
                if(*begin == ',') {
                    begin ++;
                }
                containerI ++;
            }
        }
        ctx.setError(DeserializationContext::INTERNAL_ERROR, end - begin);
        return false;
    }
};

template <d::JSONMapValue Src, d::ConstString Str>
class J<Src, Str> : public Src{
    using ItemType = typename Src::mapped_type;
    using KeyType = typename Src::key_type;
public:
    using JSONValueKind = d::JSONValueKindEnumMap;
    static constexpr auto FieldName = Str;
    static_assert(FieldName.check() == true, "Please, use printable chars in values keys");

    Src& operator = (const Src& other) {
        return static_cast<Src &>(*this) = other;
    }
    J() = default;
    J(const Src & other): Src(other) {}


    bool SerializeInternal(SerializerOutputCallbackConcept auto && clb) const {
        if(char v[] = "{"; !clb(v, sizeof(v)-1))  [[unlikely]] {
            return false;
        }
        std::size_t last = static_cast<const Src&>(*this).size()-1;
        std::size_t i = 0;
        for(const auto & [key, item]: static_cast<const Src&>(*this)) {

            if(char v[] = "\""; !clb(v, sizeof(v)-1)) [[unlikely]] {
                return false;
            }
            if(!clb(reinterpret_cast<const char*>(key.data()), key.size() * sizeof(typename KeyType::value_type))) [[unlikely]] {
                return false ;
            }
            if(char v[] = "\":"; !clb(v,  sizeof(v)-1)) [[unlikely]] {
                return false;
            }

            if(!item.SerializeInternal(std::forward<std::decay_t<decltype(clb)>>(clb))) [[unlikely]] {
                return false;
            }
            if(i != last) {
                if(char v[] = ","; !clb(v, sizeof(v)-1))  [[unlikely]] {
                    return false;
                }
            }
            i++;
        }
        if(char v[] = "}"; !clb(v, sizeof(v)-1))  [[unlikely]] {
            return false;
        }
        return true;
    }

    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool DeserializeInternal(InpIter & begin, const InpIter & end, DeserializationContext & ctx) {
        if(!d::skipWhiteSpaceTill(begin, end, '{', ctx)) [[unlikely]] {
            return false;
        }
        static_cast<Src&>(*this).clear();

        while(begin != end) {
            if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                return false;
            }
            if(*begin == '}') {
                begin ++;
                return true;
            }
            KeyType keyContainer;
            if(!d::extractJSString(begin, end, ctx, keyContainer)) [[unlikely]] {
                return false;
            }

            if(!d::skipWhiteSpaceTill(begin, end, ':', ctx)) [[unlikely]] {
                return false;
            }
            if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                return false;
            }

            auto kvI = static_cast<Src&>(*this).try_emplace(keyContainer).first;
            if(end-begin>=4 && *(begin+0) == 'n'&&*(begin+1) == 'u'&&*(begin+2) == 'l'&&*(begin+3) == 'l') {
                begin += 4;
            } else {
                ItemType  & newItem = kvI->second;
                if(! newItem.DeserializeInternal(begin, end, ctx)) {
                    return false;
                }
            }

            if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                return false;
            }
            if(*begin == ',') {
                begin ++;
            }
        }
        ctx.setError(DeserializationContext::INTERNAL_ERROR, end - begin);
        return false;
    }
};

namespace d {

template <std::size_t Index, class PotentialJsonFieldT> struct KeyIndexEntry {
    static constexpr bool skip = true;
};
template <std::size_t Index, JSONWrappedValueCompatible JsonFieldT>
struct KeyIndexEntry<Index, JsonFieldT>{
    static constexpr bool skip = false;
    static constexpr auto FieldName =  JsonFieldT::FieldName;
    static constexpr std::size_t OriginalIndex = Index;
};

template <class T, class I> struct KeyIndexBuilder;
template <class ... FieldTypes, std::size_t ... Is>
struct KeyIndexBuilder<std::tuple<FieldTypes...>, std::index_sequence<Is...>>  {
    using VarT = swl::variant<KeyIndexEntry<Is, FieldTypes>...>;

    static constexpr std::size_t jsonFieldsCount = (0 + ... + (KeyIndexEntry<Is, FieldTypes>::skip ? 0: 1));
    using KeyIndexEntryArrayType = std::array<VarT, jsonFieldsCount>;

    struct visitor {
        template <class KeyIndexType>
        constexpr  std::string_view operator()(KeyIndexType keyIndex) {
            if constexpr(KeyIndexType::skip == false) {
                return keyIndex.FieldName.toStringView();
            } else
                return {};
        }
    };
    struct ProjKeyIndexVariantToStringView{
        constexpr std::string_view operator()(VarT val) const {
            return swl::visit(visitor{}, val);
        }
    };

    static constexpr KeyIndexEntryArrayType sortKeyIndexes(std::array<VarT, sizeof... (Is)>  vals) {
        KeyIndexEntryArrayType ret;
        std::size_t j = 0;
        for(std::size_t i = 0; i < vals.size(); i++) {
            if(swl::visit([](auto v){ return !v.skip; }, vals[i])) {
                ret[j] = vals[i];
                j++;
            }
        }
        std::ranges::sort(ret, std::ranges::less{}, ProjKeyIndexVariantToStringView{});

//        static_assert (std::unique(ret.begin(), ret.end()) == ret.end(), "Keys must be unique" );
        return ret;
    }
    static constexpr KeyIndexEntryArrayType sortedKeyIndexArray = sortKeyIndexes({KeyIndexEntry<Is, FieldTypes>()...});
};

template<class ForwardIt, class T, class Compare, class Proj>
ForwardIt lower_bound(ForwardIt first, ForwardIt last, const T& value, Compare comp, Proj proj)
{
    ForwardIt it;
    typename std::iterator_traits<ForwardIt>::difference_type count, step;
    count = std::distance(first, last);

    while (count > 0) {
        it = first;
        step = count / 2;
        std::advance(it, step);
        if (comp(proj(*it), value)) {
            first = ++it;
            count -= step + 1;
        }
        else
            count = step;
    }
    return first;
}

template<std::forward_iterator I, std::sentinel_for<I> S, class T,
         class Proj = std::identity,
         std::indirect_strict_weak_order<
             const T*,
             std::projected<I, Proj>> Comp = std::ranges::less>
constexpr I binary_search(I first, S last, const T& value, Comp comp = {}, Proj proj = {})
{
    first = lower_bound(first, last, value, comp, proj);
    if (!(first == last) && !(comp(value, proj(*first)))) {
        return first;
    } else {
        return last;
    }
}
}

template <d::JSONObjectValue Src, d::ConstString Str>
class J<Src, Str> : public Src {
    using KeyIndexBuilderT = d::KeyIndexBuilder<decltype (pfr::structure_to_tuple(std::declval<Src>())), std::make_index_sequence<pfr::tuple_size_v<Src>>>;
    static constexpr auto sortedKeyIndexArray = KeyIndexBuilderT::sortedKeyIndexArray;

    using FilledFlagsArray = std::array<bool, sortedKeyIndexArray.size()>;
    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool DeserialiseField(const InpIter & keyBegin, const InpIter & keyEnd, InpIter &begin, const InpIter & end, DeserializationContext & ctx, bool fieldisNull, FilledFlagsArray & filledFlagsArray) {
        std::string_view keySV{keyBegin, keyEnd};

        auto foundVarIt = d::binary_search(sortedKeyIndexArray.begin(), sortedKeyIndexArray.end(), keySV, std::ranges::less{}, typename KeyIndexBuilderT::ProjKeyIndexVariantToStringView{});

        if(foundVarIt == sortedKeyIndexArray.end()) {
            if(ctx.flag(ParseFlags::EXCESS_FIELDS_PROHIBITED)) {
                ctx.setError(DeserializationContext::EXCESS_FIELD, end - begin);
                return false;
            }
            if(!fieldisNull) {
                std::uint8_t level = d::SkippingMaxNestingLevel;
                return d::skipJsonValue(level, begin, end, ctx);
            } else {
                return true;
            }
        }


        bool deserRes = swl::visit(
                [this, &begin, &end, &ctx, fieldisNull]<class KeyIndexType>(KeyIndexType keyIndex) -> bool{
                    if constexpr(KeyIndexType::skip == false) {
                        using FieldType = pfr::tuple_element_t<KeyIndexType::OriginalIndex, Src>;
                        FieldType & f = pfr::get<KeyIndexType::OriginalIndex>(static_cast<Src &>(*this));
                        if(fieldisNull) {
                            f = FieldType{};
                            return true;
                        }  else {
                            return f.DeserializeInternal(begin, end, ctx);
                        }
                    } else
                        return false;
                }
        , *foundVarIt);
        if(deserRes) {
            filledFlagsArray[foundVarIt-sortedKeyIndexArray.begin()] = true;
        }

        return deserRes;
    }
public:
    using JSONValueKind = d::JSONValueKindEnumObject;
    static constexpr auto FieldName = Str;
    static_assert(FieldName.check() == true, "Please, use printable chars in values keys");

    J(const Src & other): Src(other) {}
    J() = default;

    Src& operator = (const Src& other) {
        return static_cast<Src &>(*this) = other;
    }

    bool SerializeInternal(SerializerOutputCallbackConcept auto && clb) const {
        std::size_t last_index = 0;
        pfr::for_each_field(static_cast<const Src &>(*this), [&last_index]<class T>(const T & val, std::size_t index){
            if constexpr(d::JSONWrappedValueCompatible<T>) { last_index = index;}
        });
        if(char v[] = "{"; !clb(v, sizeof(v)-1)) {
            return false;
        }
        bool r = true;
        auto visitor = [&clb, last_index, &r]<class T>(const T & val, std::size_t index) {
            if(!r) return;
            if constexpr(d::JSONWrappedValueCompatible<T>) {
                if(char v[] = "\""; !clb(v, sizeof(v)-1))[[unlikely]]   {
                    r = false;
                    return;
                }
                if(!clb(reinterpret_cast<const char*>(val.FieldName.m_data), val.FieldName.Length * val.FieldName.CharSize)) {
                    r = false;
                    return;
                }
                if(char v[] = "\":"; !clb(v,  sizeof(v)-1)) [[unlikely]] {
                    r = false;
                    return;
                }
                if(!val.SerializeInternal(std::forward<std::decay_t<decltype(clb)>>(clb)))[[unlikely]]  {
                    r = false;
                    return;
                }
                if(index != last_index) {
                    if(char v[] = ","; !clb(v, sizeof(v)-1)) [[unlikely]] {
                        r = false;
                        return;
                    }
                }
            }
        };
        pfr::for_each_field(static_cast<const Src &>(*this), visitor);
        if(!r) [[unlikely]]  {
            return false;
        }
        if(char v[] = "}"; !clb(v, sizeof(v)-1)) [[unlikely]] {
            return false;
        }
        return true;
    }

    template <class T>
        requires (!SerializerOutputCallbackConcept<T>)
    bool Serialize(T & container) const {
        return SerializeInternal([&container](const char * data, std::size_t size){
            container.insert(container.end(), data, data + size);
            return true;
        });
    }
    bool Serialize(SerializerOutputCallbackConcept auto && clb) const {
        return SerializeInternal(std::forward<std::decay_t<decltype(clb)>>(clb));
    }

    template<class InpIter> requires InputIteratorConcept<InpIter>
    DeserializationContext Deserialize(InpIter begin, const InpIter & end, ParseFlags flags = ParseFlags::DEFAULT) {
        DeserializationContext ctx(end-begin, flags);
        bool ret = DeserializeInternal(begin, end, ctx);
        return ctx;
    }

    template<class ContainterT> requires std::ranges::range<ContainterT>
    DeserializationContext Deserialize(const ContainterT & c, ParseFlags flags = ParseFlags::DEFAULT) {
        DeserializationContext ctx(c.size(), flags);
        auto b = c.begin();
        bool ret =  DeserializeInternal(b, c.end(), ctx);
        return ctx;
    }

    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool DeserializeInternal(InpIter & begin, const InpIter & end, DeserializationContext & ctx) {
        if(!d::skipWhiteSpaceTill(begin, end, '{', ctx)) [[unlikely]] {
            return false;
        }

        FilledFlagsArray filledFlags;
        filledFlags.fill(false);
        while(begin != end) {
            if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                return false;
            }
            if(*begin == '}') {
                begin ++;

                for(int i = 0; i < filledFlags.size(); i ++) {
                    if(!filledFlags[i]) {
                        if(ctx.flag(ParseFlags::ALL_FIELDS_REQUIRED)) {
                            ctx.setError(DeserializationContext::MISSING_FIELD, end - begin);
                            return false;
                        } else {
                            swl::visit([this]<class KeyIndexType>(KeyIndexType keyIndex){
                                   if constexpr(KeyIndexType::skip == false) {
                                       using FieldType = pfr::tuple_element_t<KeyIndexType::OriginalIndex, Src>;
                                       FieldType & f = pfr::get<KeyIndexType::OriginalIndex>(static_cast<Src &>(*this));
                                       f = FieldType{};

                                   }
                               }
                            , *(sortedKeyIndexArray.begin() + i));
                        }
                    }
                }
                return true;
            }

            if(!d::skipWhiteSpaceTill(begin, end, '"', ctx)) [[unlikely]] {
                return false;
            }
            InpIter keyBegin = begin;

            InpIter keyEnd = d::findJsonStringEnd(keyBegin, end, ctx);
            if(keyEnd == end) [[unlikely]] {
                return false;
            }
            begin = keyEnd;
            begin ++;
            if(begin == end) [[unlikely]] {
                ctx.setError(DeserializationContext::UNEXPECTED_END_OF_DATA, end - begin);
                return false;
            }
            if(!d::skipWhiteSpaceTill(begin, end, ':', ctx)) [[unlikely]] {
                return false;
            }
            if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                return false;
            }
            bool fieldisNull = false;
            if(end-begin>=4 && *(begin+0) == 'n'&&*(begin+1) == 'u'&&*(begin+2) == 'l'&&*(begin+3) == 'l') {
                begin += 4;
                fieldisNull = true;
            }
            if(!DeserialiseField(keyBegin, keyEnd, begin, end, ctx, fieldisNull, filledFlags)) {
                return false;
            }

            if(!d::skipWhiteSpace(begin, end, ctx)) [[unlikely]] {
                return false;
            }
            if(*begin == ',') {
                begin ++;
            }
        }
        ctx.setError(DeserializationContext::INTERNAL_ERROR, end - begin);
        return false;
    }
};


}
#endif // CPP_JSON_REFLECTION_HPP
