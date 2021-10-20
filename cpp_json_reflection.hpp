#ifndef CPP_JSON_REFLECTION_HPP
#define CPP_JSON_REFLECTION_HPP

#include <pfr.hpp>
#include <type_traits>
#include <concepts>
#include <cmath>

#define JSMN_HEADER
#define JSMN_STRICT

#include "jsmn.h"
#include <inttypes.h>
#include <iterator>
#include <locale>
#include <swl/variant.hpp>
#include <algorithm>

namespace JSONReflection {


template <typename CharT, std::size_t N> struct JSONFieldNameStringLiteral
{
    static constexpr bool checkQuotes(const CharT (&foo)[N+1]) {
        for(int i = 0; i <= N; i ++) {
            if(foo[i]=='"' && foo[i-1] != '\\') return false;
        }
        return true;
    }
    constexpr JSONFieldNameStringLiteral(const CharT (&foo)[N+1]) {
//        static_assert(checkQuotes(foo));
        std::copy_n(foo, N+1, m_data);
    }
    CharT m_data[N+1];
    static constexpr std::size_t Length = N;
    static constexpr std::size_t CharSize = sizeof (CharT);
};
template <typename CharT, std::size_t N>
JSONFieldNameStringLiteral(const CharT (&str)[N])->JSONFieldNameStringLiteral<CharT, N-1>;

struct JSONValueKindEnumPlain {};
struct JSONValueKindEnumObject {};
struct JSONValueKindEnumArray {};


template<typename T>
concept JSONWrappedValue = requires {
        typename T::JSONValueKind;
        std::is_same_v<typename T::JSONValueKind, JSONValueKindEnumPlain>
        || std::is_same_v<typename T::JSONValueKind, JSONValueKindEnumObject>
        || std::is_same_v<typename T::JSONValueKind, JSONValueKindEnumArray>;
        };



template <typename T>
concept StringTypeConcept = requires (T && v) {
    typename T::value_type;
    std::is_pointer_v<decltype(v.data())>;
    std::is_integral_v<std::remove_pointer_t<decltype(v.data())>>;
    {v.size()} -> std::convertible_to<std::size_t>;
    requires !JSONWrappedValue<typename T::value_type>;
};

template <typename T>
concept SerializerOutputCallbackConcept = requires (T && clb) {
    {clb(std::declval<const char*>(), std::declval<std::size_t>())} -> std::convertible_to<bool>;
};

template<typename T>
concept JSONBasicValue = std::same_as<T, bool> || std::same_as<T, double>|| std::convertible_to<T, std::int64_t> || StringTypeConcept<T>;

template<typename T>
concept JSONArrayValue = std::ranges::range<T> && !JSONBasicValue<T> && JSONWrappedValue<typename T::value_type>;

template<typename T>
concept JSONObjectValue = std::is_class_v<T> && !JSONArrayValue<T> && !JSONBasicValue<T>;



template <JSONFieldNameStringLiteral Str = "">
struct A {
    static constexpr auto str = Str;
};

using hello_A = A<"hello">;
static_assert (decltype(hello_A::str)::Length == 5);

using hello_A2 = A<>;
static_assert (decltype(hello_A2::str)::Length == 0);


template <class Src, JSONFieldNameStringLiteral Str = "">
requires JSONBasicValue<Src> || JSONArrayValue<Src> || JSONObjectValue<Src>
class JS {
};

bool skipWhiteSpace(std::forward_iterator auto & begin, const std::forward_iterator auto & end) {
    while(begin != end && std::isspace(*begin, std::locale::classic()))
        begin ++;
    if (begin == end) return false;
    return true;
}


bool skipJsonValue(std::forward_iterator auto & begin, const std::forward_iterator auto & end) {
    return false;
}



bool skipTill(std::forward_iterator auto & begin, const std::forward_iterator auto & end, char s) {
    while(begin != end && *begin != s)
        begin ++;
    if (begin == end) return false;
    return true;
}

bool skipWhiteSpaceTill(std::forward_iterator auto & begin, const std::forward_iterator auto & end, char s) {
    while(begin != end) {
        if(std::isspace(*begin, std::locale::classic())) {
           begin ++;
        } else if(*begin == s) {
            begin ++;
            break;
        }
    }
    if (begin == end) return false;
    return true;
}

template<class InpIter> requires std::forward_iterator<InpIter>
InpIter findJsonKeyStringEnd(InpIter begin, const InpIter & end) {
    for(; begin != end; begin ++) {
        char c = reinterpret_cast<char>(*begin);
        if(c == '"') {
            return begin;
        } else if (c == '\\' && std::next(begin) != end) {
            int i;
            begin++;
            switch (*begin) {
                /* Allowed escaped symbols */
                case '\"':
                case '/':
                case '\\':
                case 'b':
                case 'f':
                case 'r':
                case 'n':
                case 't':
                    break;
            /* Allows escaped symbol \uXXXX */
                case 'u': {
                    begin++;
                    auto beforeEscapedSymbol = begin;
                    for (int i = 0; i < 4 && begin != end ; i++) {
                        /* If it isn't a hex character we have an error */
                        auto currChar = *begin;
                        if (!((currChar >= 48 && currChar <= 57) ||     /* 0-9 */
                              (currChar >= 65 && currChar <= 70) ||     /* A-F */
                              (currChar >= 97 && currChar <= 102)  )) { /* a-f */
                            return end;
                        }
                        beforeEscapedSymbol = begin;
                        begin++;
                    }
                    begin = beforeEscapedSymbol;
                    }
                    break;
            /* Unexpected symbol */
                default:
                    return end;
            }
        }
    }

    return end;
}



template <JSONBasicValue Src, JSONFieldNameStringLiteral Str>
class JS<Src, Str> {

    Src content;
public:
    static constexpr auto FieldName = Str;
    using JSONValueKind = JSONValueKindEnumPlain;

    operator Src&() {
        return content;
    }
    JS(): content(Src())  {

    }
    JS(const Src & val):content(val)  {

    }

    Src & operator=(const Src & other) {
        return content = other;
    }

    bool Serialize(SerializerOutputCallbackConcept auto && clb) const {
        if constexpr(std::same_as<bool, Src>) {
            if(content) {
                char v[] = "true";
                return clb(v, sizeof(v));
            }
            else {
                char v[] = "false";
                return clb(v, sizeof(v));
            }
        } else if constexpr(StringTypeConcept<Src>) {
            if(char v[] = "\""; !clb(v, sizeof(v))) {
                return false;
            }
            if(!clb(reinterpret_cast<const char*>(content.data()), content.size() * sizeof (typename Src::value_type))) {
                return false;
            }
            if(char v[] = "\""; !clb(v, sizeof(v))) {
                return false;
            }
            return true;
        } else if constexpr(std::same_as<double, Src>) {
            if(std::isnan(content)) {
                char v[] = "0";
                return clb(v, 1);
            } else {
                char buf[25];
                int s = snprintf(buf, sizeof (buf), "%g", content);
                if(s == sizeof (buf)) return false;
                else return clb(buf, s);
            }
        } else if constexpr(std::same_as<std::int64_t, Src>) {
            char buf[25];
            int s = snprintf(buf,  sizeof (buf), "%" PRId64, content);
            if(s == sizeof (buf)) return false;
            else return clb(buf, s);
        }
    }

    template<class InpIter> requires std::forward_iterator<InpIter>
    bool DeserialiseInternal(InpIter & begin, const InpIter & end) {
       return false;
    }
};

template <JSONArrayValue Src, JSONFieldNameStringLiteral Str>
class JS<Src, Str> : public Src{
public:
    static constexpr auto FieldName = Str;

    using JSONValueKind = JSONValueKindEnumArray;
    bool Serialize(SerializerOutputCallbackConcept auto && clb) const {
        if(char v[] = "["; !clb(v, sizeof(v))) {
            return false;
        }
        std::size_t last = static_cast<Src>(*this).size()-1;
        std::size_t i = 0;
        for(const auto & item: static_cast<Src>(*this)) {
            if(!item.Serialize(std::forward<std::decay_t<decltype(clb)>>(clb))) {
                return false;
            }
            if(i != last) {
                if(char v[] = ","; !clb(v, sizeof(v))) {
                    return false;
                }
            }
            i++;
        }

        char v[] = "]";
        return clb(v, sizeof(v));
    }

    template<class InpIter> requires std::forward_iterator<InpIter>
    bool DeserialiseInternal(InpIter & begin, const InpIter & end) {
       return false;
    }
};

template <std::size_t Index, class PotentialJsonFieldT> struct KeyIndexEntry {
    static constexpr bool skip = true;
};
template <std::size_t Index, class InnerFieldType, JSONFieldNameStringLiteral KeyString>
struct KeyIndexEntry<Index, JS<InnerFieldType, KeyString>>{
    static constexpr bool skip = false;
    static constexpr auto FieldName =  KeyString;
    static constexpr std::size_t OriginalIndex = Index;
};

template <class T, class I> struct KeyIndexBuilder;
template <class ... FieldTypes, std::size_t ... Is>
struct KeyIndexBuilder<std::tuple<FieldTypes...>, std::index_sequence<Is...>>  {
    using VarT = swl::variant<KeyIndexEntry<Is, FieldTypes>...>;

    static constexpr std::size_t jsonFieldsCount = (0 + ... + (KeyIndexEntry<Is, FieldTypes>::skip ? 0: 1));
    using KeyIndexEntryArrayType = std::array<VarT, jsonFieldsCount>;

    static constexpr KeyIndexEntryArrayType sortKeyIndexes(std::array<VarT, sizeof... (Is)>  vals) {
        KeyIndexEntryArrayType ret;
        std::size_t j = 0;
        for(std::size_t i = 0; i < vals.size(); i++) {
            if(swl::visit([](auto v){ return !v.skip; }, vals[i])) {
                ret[j] = vals[i];
                j++;
            }
        }
        struct {
               constexpr bool operator()(VarT a, VarT b) const {
                   return swl::visit(
                               []<class FT, class ST>(FT first, ST second) -> bool{
                                   if constexpr (FT::skip == false && ST::skip == false) {
                                       std::size_t first1 = 0, first2 = 0, last1 = first.FieldName.Length, last2 = second.FieldName.Length;
                                       for ( ; (first1 != last1) && (first2 != last2); ++first1, (void) ++first2 ) {
                                          if (first.FieldName.m_data[first1] < second.FieldName.m_data[first2]) return true;
                                          if (second.FieldName.m_data[first2] < first.FieldName.m_data[first1]) return false;
                                       }
                                       return (first1 == last1) && (first2 != last2);
                                   } else
                                   return false;
                               }
                               , a, b);
               }
        } customLess;

        std::ranges::sort(ret, customLess);
        return ret;
    }
    static constexpr KeyIndexEntryArrayType sortedKeyIndexArray = sortKeyIndexes({KeyIndexEntry<Is, FieldTypes>()...});

    //TODO remove later

    using testTupleT = std::tuple<KeyIndexEntry<Is, FieldTypes>...>;
    using testTupleT2 = std::tuple<FieldTypes...>;
};


template <JSONObjectValue Src, JSONFieldNameStringLiteral Str>
class JS<Src, Str> : public Src {
public:
    using JSONValueKind = JSONValueKindEnumObject;
    static constexpr auto FieldName = Str;

    bool Serialize(SerializerOutputCallbackConcept auto && clb) const {
        std::size_t last_index = 0;
        pfr::for_each_field(static_cast<Src>(*this), [&last_index]<class T>(const T & val, std::size_t index){
            if constexpr(JSONWrappedValue<T>) { last_index = index;}
        });
        if(char v[] = "{"; !clb(v, sizeof(v))) {
            return false;
        }
        bool r = true;
        auto visitor = [&clb, last_index, &r]<class T>(const T & val, std::size_t index) {
            if(!r) return;
            if constexpr(JSONWrappedValue<T>) {
                if(char v[] = "\""; !clb(v, sizeof(v))) {
                    r = false;
                    return;
                }
                if(char v[] = "\""; !clb(reinterpret_cast<const char*>(val.FieldName.m_data), val.FieldName.Length * val.FieldName.CharSize)) {
                    r = false;
                    return;
                }
                if(char v[] = "\":"; !clb(v,  sizeof(v))) {
                    r = false;
                    return;
                }

                if(!val.Serialize(std::forward<std::decay_t<decltype(clb)>>(clb))) {
                    r = false;
                    return;
                }
                if(index != last_index) {
                    if(char v[] = ","; !clb(v, sizeof(v))) {
                        r = false;
                        return;
                    }
                }
            }
        };
        pfr::for_each_field(static_cast<Src>(*this), visitor);
        if(!r) return false;
        if(char v[] = "}"; !clb(v, sizeof(v))) {
            return false;
        }
        return true;
    }
private:

    using KeyIndexBuilderT = KeyIndexBuilder<decltype (pfr::structure_to_tuple(std::declval<Src>())), std::make_index_sequence<pfr::tuple_size_v<Src>>>;
    static constexpr auto sortedKeyIndexArray = KeyIndexBuilderT::sortedKeyIndexArray;

public:


    template<class InpIter> requires std::forward_iterator<InpIter>
    bool DeserialiseField(const InpIter & keyBegin, const InpIter & keyEnd, InpIter &begin, const InpIter & end) {
        struct proj{
            int offset = 0;
            char operator()(typename KeyIndexBuilderT::VarT val) const {
                return swl::visit(
                        [this]<class KeyIndexType>(KeyIndexType keyIndex) -> char{
                                if constexpr(KeyIndexType::skip == false) {
                                    return keyIndex.FieldName.m_data[offset];
                                } else
                                    return '\0';
                        }
               , val);
            }
        };
        int namesOffs = 0;
        for(auto sI = keyBegin; sI != keyEnd; sI ++) {
            auto rng = std::ranges::equal_range(sortedKeyIndexArray, *keyBegin,  std::ranges::less{}, proj{namesOffs});
            if(rng.empty()) {
                return skipJsonValue(begin, end);
            } else if(rng.size() == 1) {
                return swl::visit(
                        [this, &begin, &end]<class KeyIndexType>(KeyIndexType keyIndex) -> bool{
                                if constexpr(KeyIndexType::skip == false) {
                                    using FieldType = pfr::tuple_element_t<KeyIndexType::OriginalIndex, Src>;
//                                    pfr::get<KeyIndexType::OriginalIndex>(static_cast<Src>(*this)) = false;
//                                    FieldType & field = pfr::get<KeyIndexType::OriginalIndex>(static_cast<Src>(*this));
//                                    return field.DeserialiseInternal(begin, end, stoppedAt);
                                    return pfr::get<KeyIndexType::OriginalIndex>(static_cast<Src>(*this)).DeserialiseInternal(begin, end);

                                } else
                                    return false;
                        }
               , rng.front());
            }
            namesOffs ++;
        }

        return false;
    }
    template<class InpIter> requires std::forward_iterator<InpIter>
    bool Deserialise(InpIter begin, const InpIter & end) {
        return DeserialiseInternal(begin, end);
    }

    template<class InpIter> requires std::forward_iterator<InpIter>
    bool DeserialiseInternal(InpIter & begin, const InpIter & end) {
        if(!skipWhiteSpaceTill(begin, end, '{')) return false;

        while(begin != end) {
            if(!skipWhiteSpace(begin, end)) return false;
            if(*begin == '}') {
                begin ++;
                return true;
            }

            if(!skipWhiteSpaceTill(begin, end, '"')) return false;
            InpIter keyBegin = begin;

            InpIter keyEnd = findJsonKeyStringEnd(keyBegin, end);
            if(keyEnd == end)
                return false;
            begin = keyEnd;
            begin ++;
            if(begin == end)
                return false;
            if(!skipWhiteSpaceTill(begin, end, ':')) return false;

            bool ok = DeserialiseField(keyBegin, keyEnd, begin, end);
            if(!ok) return false;

            if(!skipWhiteSpace(begin, end)) return false;
            if(*begin == ',') {
                begin ++;
            }
        }

        return false;
    }

    //TODO remove later
    using testTupleT = typename KeyIndexBuilderT::testTupleT;
    using testTupleT2 = typename KeyIndexBuilderT::testTupleT2;
    static constexpr auto jsonFieldsCount = KeyIndexBuilderT::jsonFieldsCount;
    static constexpr auto sortedKeyIndexArrayTemp = KeyIndexBuilderT::sortedKeyIndexArray;

};

//class Deserializer
}
#endif // CPP_JSON_REFLECTION_HPP
