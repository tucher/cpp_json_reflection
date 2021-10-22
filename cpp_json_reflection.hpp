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
namespace simdjson {
namespace internal {
char *to_chars(char *first, const char *last, double value);
}
}
namespace JSONReflection {


template <typename CharT, std::size_t N> struct JSONFieldNameStringLiteral
{
    constexpr JSONFieldNameStringLiteral(const CharT (&foo)[N+1]) {
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
    {v[0]} -> std::convertible_to<typename T::value_type>;
    requires !JSONWrappedValue<typename T::value_type>;
};

template <typename T>
concept DynamicContainerTypeConcept = requires (T && v) {
        typename T::value_type;
        v.push_back(std::declval<typename T::value_type>());
        v.clear();
};

template <typename T>
concept ReserveCapable = DynamicContainerTypeConcept<T> && requires (T && v) {

        v.reserve(10);
};


template <typename T>
concept DynamicStringTypeConcept = StringTypeConcept<T> && DynamicContainerTypeConcept<T>;

template <typename T>
concept SerializerOutputCallbackConcept = requires (T && clb) {
    {clb(std::declval<const char*>(), std::declval<std::size_t>())} -> std::convertible_to<bool>;
};

template <typename T>
concept SerializerOutputBufferConcept = requires (T && buf) {
    { T::Size  } -> std::convertible_to<std::size_t>;
    { buf.data } -> std::same_as<char *>;
};


template<typename T>
concept JSONBasicValue = std::same_as<T, bool> || std::same_as<T, double>|| std::convertible_to<T, std::int64_t> || StringTypeConcept<T>;

template<typename T>
concept JSONArrayValue = std::ranges::range<T> && !JSONBasicValue<T> && JSONWrappedValue<typename T::value_type>;

template<typename T>
concept JSONObjectValue = std::is_class_v<T> && !std::ranges::range<T> && !JSONArrayValue<T> && !JSONBasicValue<T>;

template<typename InpIter>
concept InputIteratorConcept =  std::forward_iterator<InpIter> && requires(InpIter inp) {
    {*inp} -> std::convertible_to<char>;
};

template<typename T>
concept JSONWrapable = JSONBasicValue<T> || JSONArrayValue<T> || JSONObjectValue<T>;
template <class Src, JSONFieldNameStringLiteral Str = "">
class J {
    template <class... T>
    static constexpr bool always_false = false;
    static_assert(always_false<Src>, "JSONReflection: Cannot represent type in terms of JSON. See next compiler messages for details");
    static_assert (JSONWrapable<Src>);
};

inline bool isSpace(char a) {
    return a == 0x20 || a == 0x0A || a == 0x0D || a == 0x09;
}

inline bool isPlainEnd(char a) {
    return isSpace(a) || a == ']' || a == ',' || a == '}';
}
bool skipWhiteSpace(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end) {
    while(isSpace(*begin) && begin != end)
        begin ++;
    if (begin == end) return false;
    return true;
}

bool skipTillPlainEnd(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end) {
    while(!isSpace(*begin)
          && *begin != ','
          && *begin != '}'
          && *begin != ']' && begin != end)
        begin ++;
    if (begin == end) return false;
    return true;
}

bool skipJsonValue(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end) {
    if(!skipWhiteSpace(begin, end)) return false;
    int level = 0;
    enum class Where {
        IN_OBJ,
        IN_ARRAY,
        IN_PLAIN
    };
    Where wh;
    if(*begin != '[' && *begin != '{') {
        return skipTillPlainEnd(begin, end);
    }

    do {
        switch(*begin) {
        case '[':
            wh = Where::IN_ARRAY;
            level ++;
            break;
        case '{':
            wh = Where::IN_OBJ;
            level ++;
            break;
        case '}':
//            if(wh != Where::IN_OBJ) return false;
            level --;
            break;
        case ']':
            level --;
            break;
        }

       begin ++;

    } while(level != 0 && begin != end);

    if(begin == end)
        return false;

    return true;
}



bool skipTill(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, char s) {
    while( *begin != s && begin != end)
        begin ++;
    if (begin == end) return false;
    return true;
}

bool skipWhiteSpaceTill(InputIteratorConcept auto & begin, const InputIteratorConcept auto & end, char s) {
    if(!skipWhiteSpace(begin, end)) return false;
    if(*begin == s) {
        begin ++;
        if (begin == end) return false;
        return true;
    }
    return false;
}

template<class InpIter> requires InputIteratorConcept<InpIter>
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
class J<Src, Str> {

    Src content;
public:
    static constexpr auto FieldName = Str;
    using JSONValueKind = JSONValueKindEnumPlain;

    operator Src&() {
        return content;
    }
    operator const Src&() const {
        return content;
    }
    constexpr J(): content(Src())  {

    }
    J(const Src & val):content(val)  {

    }

    Src & operator=(const Src & other) {
        return content = other;
    }
    auto operator<=>(const J&) const = default;
    auto operator<=>(const Src& rhs) const {
        return content <=> rhs;
    }
//    auto operator==(const J& rhs) const {
//        return content == rhs.content;
//    }
    auto operator==(const Src& rhs) const {
        return content == rhs;
    }
    auto operator!=(const Src& rhs) const {
        return content != rhs;
    }

    bool Serialize(SerializerOutputCallbackConcept auto && clb) const {
        if constexpr(std::same_as<bool, Src>) {
            if(content) {
                char v[] = "true";
                return clb(v, sizeof(v)-1);
            }
            else {
                char v[] = "false";
                return clb(v, sizeof(v)-1);
            }
        } else if constexpr(StringTypeConcept<Src>) {
            if(char v[] = "\""; !clb(v, sizeof(v)-1)) {
                return false;
            }
            if constexpr(DynamicStringTypeConcept<Src>) {
                if(!clb(reinterpret_cast<const char*>(content.data()), content.size() * sizeof (typename Src::value_type))) {
                    return false;
                }
            } else {
                std::size_t zp = 0;
                while(zp < content.size () && content[zp] != 0) zp ++;
                if(!clb(reinterpret_cast<const char*>(content.data()), zp * sizeof (typename Src::value_type))) {
                    return false;
                }
            }
            if(char v[] = "\""; !clb(v, sizeof(v)-1)) {
                return false;
            }
            return true;
        } else if constexpr(std::same_as<double, Src> || std::same_as<std::int64_t, Src>) {
            if(std::isnan(content) || std::isinf(content)) {
                char v[] = "0";
                return clb(v, 1);
            } else {
                char buf[50];

                char * endChar = simdjson::internal::to_chars(buf, buf + sizeof (buf), content);
                if(endChar-buf == sizeof (buf)) return false;
                else return clb(buf, endChar-buf);
            }
        } else
            return true;
    }

    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool DeserialiseInternal(InpIter & begin, const InpIter & end) {
        if(!skipWhiteSpace(begin, end)) return false;

        if constexpr(std::same_as<bool, Src>) {
            char fc = *begin;
            begin++;
            if(fc == 't') {
                constexpr char v[] = "rue";
                for(int i = 0; i < 3; i ++) {
                    if(begin == end) {
                        return false;
                    }
                    if(*begin != v[i]) {
                        return false;
                    }
                    begin ++;
                }
                if(!isPlainEnd(*begin)) return false;
                content = true;
                return true;
            }
            else if(fc == 'f') {
                constexpr char v[] = "alse";
                for(int i = 0; i < 4; i ++) {
                    if(begin == end) {
                        return false;
                    }
                    if(*begin != v[i]) {
                        return false;
                    }
                    begin ++;
                }
                if(!isPlainEnd(*begin)) return false;
                content = false;
                return true;
            } else
                return false;
        } else if constexpr(StringTypeConcept<Src>) {
            if(!skipWhiteSpaceTill(begin, end, '"'))
                return false;

            auto strBegin = begin;
            auto strEnd = findJsonKeyStringEnd(begin, end);
            if(strEnd == end)
                return false;
            begin = strEnd;
            begin ++;
            if(begin == end)
                return false;

            if constexpr (DynamicStringTypeConcept<Src>) { //dynamic
                content.clear();
                while(strBegin != strEnd) {
                    content.push_back(*strBegin);
                    strBegin ++;
                }
                return true;
            } else { // static string
                std::size_t index = 0;
                while(strBegin != strEnd) {
                    content[index] = *strBegin;
                    strBegin ++;
                    index ++;
                    if(index > content.size()) {
                        return false;
                    }
                }
                for(; index < content.size(); index ++) {
                    content[index] = '\0';
                }
            }

            return true;
        } else if constexpr(std::same_as<double, Src> || std::same_as<std::int64_t, Src>) {
            char buf[40];
            std::size_t index = 0;
            while(!isPlainEnd(*begin) &&begin != end ) {
                buf[index] = *begin;
                begin ++;
                index ++;
                if(index > sizeof (buf) - 1) {
                    return false;
                }
            }
            buf[index] = 0;
            double x;
            if(fast_double_parser::parse_number(buf, &x) != nullptr) {
                content = x;
                return true;
            }
            return false;
        } else {
            return false;
        }
    }
};

template <JSONArrayValue Src, JSONFieldNameStringLiteral Str>
class J<Src, Str> : public Src{

public:
    using ItemType = typename Src::value_type;

    static constexpr auto FieldName = Str;

    using JSONValueKind = JSONValueKindEnumArray;
    bool Serialize(SerializerOutputCallbackConcept auto && clb) const {
        if(char v[] = "["; !clb(v, sizeof(v)-1)) {
            return false;
        }
        std::size_t last = static_cast<const Src&>(*this).size()-1;
        std::size_t i = 0;
        for(const auto & item: static_cast<const Src&>(*this)) {
            if(!item.Serialize(std::forward<std::decay_t<decltype(clb)>>(clb))) {
                return false;
            }
            if(i != last) {
                if(char v[] = ","; !clb(v, sizeof(v)-1)) {
                    return false;
                }
            }
            i++;
        }

        char v[] = "]";
        return clb(v, sizeof(v)-1);
    }

    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool DeserialiseInternal(InpIter & begin, const InpIter & end) {
        if(!skipWhiteSpaceTill(begin, end, '[')) return false;
        if constexpr (DynamicContainerTypeConcept<Src>) {
            static_cast<Src&>(*this).clear();
            if constexpr (ReserveCapable<Src>) {
//                static_cast<Src&>(*this).reserve(1000); //TODO via options
            }
            ItemType newItem;
            while(begin != end) {
                if(!skipWhiteSpace(begin, end)) {
                    return false;
                }
                if(*begin == ']') {
                    begin ++;
                    return true;
                }

                bool ok = newItem.DeserialiseInternal(begin, end);
                if(!ok) {
                    return false;
                }
                static_cast<Src&>(*this).push_back(newItem);

                if(!skipWhiteSpace(begin, end)) {
                    return false;
                }
                if(*begin == ',') {
                    begin ++;
                }
            }
        } else {
            int counter = 0;

            while(begin != end) {
                if(!skipWhiteSpace(begin, end)) {
                    return false;
                }
                if(*begin == ']') {
                    begin ++;
                    return true;
                }
                if(counter > static_cast<const Src&>(*this).size()) {
                    return false;
                }
                ItemType & newItem = static_cast<Src&>(*this)[counter];

                bool ok = newItem.DeserialiseInternal(begin, end);
                if(!ok) {
                    return false;
                }
                counter ++;

                if(!skipWhiteSpace(begin, end)) {
                    return false;
                }
                if(*begin == ',') {
                    begin ++;
                }
            }
        }

        return false;
    }
};

template <std::size_t Index, class PotentialJsonFieldT> struct KeyIndexEntry {
    static constexpr bool skip = true;
};
template <std::size_t Index, class InnerFieldType, JSONFieldNameStringLiteral KeyString>
struct KeyIndexEntry<Index, J<InnerFieldType, KeyString>>{
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


    struct ProjKeyIndexVariantToStringView{
        constexpr std::string_view operator()(VarT val) const {
            return swl::visit(
                    []<class KeyIndexType>(KeyIndexType keyIndex) -> std::string_view {
                            if constexpr(KeyIndexType::skip == false) {
                                return keyIndex.FieldName.toStringView();
                            } else
                                return {};
                    }
           , val);
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
        return ret;
    }
    static constexpr KeyIndexEntryArrayType sortedKeyIndexArray = sortKeyIndexes({KeyIndexEntry<Is, FieldTypes>()...});

    //TODO remove later

    using testTupleT = std::tuple<KeyIndexEntry<Is, FieldTypes>...>;
    using testTupleT2 = std::tuple<FieldTypes...>;
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
constexpr I
binary_search(I first, S last, const T& value, Comp comp = {}, Proj proj = {})
{
    first = lower_bound(first, last, value, comp, proj);
    if (!(first == last) && !(comp(value, proj(*first)))) {
        return first;
    } else {
        return last;
    }
}

template <JSONObjectValue Src, JSONFieldNameStringLiteral Str>
class J<Src, Str> : public Src {
public:
    using JSONValueKind = JSONValueKindEnumObject;
    static constexpr auto FieldName = Str;

    bool Serialize(SerializerOutputCallbackConcept auto && clb) const {
        std::size_t last_index = 0;
        pfr::for_each_field(static_cast<const Src &>(*this), [&last_index]<class T>(const T & val, std::size_t index){
            if constexpr(JSONWrappedValue<T>) { last_index = index;}
        });
        if(char v[] = "{"; !clb(v, sizeof(v)-1)) {
            return false;
        }
        bool r = true;
        auto visitor = [&clb, last_index, &r]<class T>(const T & val, std::size_t index) {
            if(!r) return;
            if constexpr(JSONWrappedValue<T>) {
                if(char v[] = "\""; !clb(v, sizeof(v)-1)) {
                    r = false;
                    return;
                }
                if(!clb(reinterpret_cast<const char*>(val.FieldName.m_data), val.FieldName.Length * val.FieldName.CharSize)) {
                    r = false;
                    return;
                }
                if(char v[] = "\":"; !clb(v,  sizeof(v)-1)) {
                    r = false;
                    return;
                }

                if(!val.Serialize(std::forward<std::decay_t<decltype(clb)>>(clb))) {
                    r = false;
                    return;
                }
                if(index != last_index) {
                    if(char v[] = ","; !clb(v, sizeof(v)-1)) {
                        r = false;
                        return;
                    }
                }
            }
        };
        pfr::for_each_field(static_cast<const Src &>(*this), visitor);
        if(!r) return false;
        if(char v[] = "}"; !clb(v, sizeof(v)-1)) {
            return false;
        }
        return true;
    }
private:

    using KeyIndexBuilderT = KeyIndexBuilder<decltype (pfr::structure_to_tuple(std::declval<Src>())), std::make_index_sequence<pfr::tuple_size_v<Src>>>;
    static constexpr auto sortedKeyIndexArray = KeyIndexBuilderT::sortedKeyIndexArray;


public:


    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool DeserialiseField(const InpIter & keyBegin, const InpIter & keyEnd, InpIter &begin, const InpIter & end) {
        std::string_view keySV{keyBegin, keyEnd};

        auto foundVarIt = binary_search(sortedKeyIndexArray.begin(), sortedKeyIndexArray.end(), keySV, std::ranges::less{}, typename KeyIndexBuilderT::ProjKeyIndexVariantToStringView{});

        if(foundVarIt == sortedKeyIndexArray.end()) {
            return skipJsonValue(begin, end);
        }

        bool deserRes = swl::visit(
                [this, &begin, &end]<class KeyIndexType>(KeyIndexType keyIndex) -> bool{
                    if constexpr(KeyIndexType::skip == false) {
                        using FieldType = pfr::tuple_element_t<KeyIndexType::OriginalIndex, Src>;
                        FieldType & f = pfr::get<KeyIndexType::OriginalIndex>(static_cast<Src &>(*this));
                        return f.DeserialiseInternal(begin, end);

                    } else
                        return false;
                }
        , *foundVarIt);

        return deserRes;
    }
    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool Deserialize(InpIter begin, const InpIter & end) {
        return DeserialiseInternal(begin, end);
    }

    template<class ContainterT> requires std::ranges::range<ContainterT>
    bool Deserialize(const ContainterT & c) {
        auto b = c.begin();
        return DeserialiseInternal(b, c.end());
    }

    template<class InpIter> requires InputIteratorConcept<InpIter>
    bool DeserialiseInternal(InpIter & begin, const InpIter & end) {
        if(!skipWhiteSpaceTill(begin, end, '{')) return false;
        while(begin != end) {
            if(!skipWhiteSpace(begin, end))
                return false;
            if(*begin == '}') {
                begin ++;
                return true;
            }

            if(!skipWhiteSpaceTill(begin, end, '"'))
                return false;
            InpIter keyBegin = begin;

            InpIter keyEnd = findJsonKeyStringEnd(keyBegin, end);
            if(keyEnd == end)
                return false;
            begin = keyEnd;
            begin ++;
            if(begin == end)
                return false;
            if(!skipWhiteSpaceTill(begin, end, ':'))
                return false;

            bool ok = DeserialiseField(keyBegin, keyEnd, begin, end);
            if(!ok)
                return false;

            if(!skipWhiteSpace(begin, end))
                return false;
            if(*begin == ',') {
                begin ++;
            }
        }

        return false;
    }

    operator Src&() {
        return static_cast<Src &>(*this);
    }

    Src& operator = (const Src& other) {
        return static_cast<Src &>(*this) = other;
    }

    //TODO remove later
    using testTupleT = typename KeyIndexBuilderT::testTupleT;
    using testTupleT2 = typename KeyIndexBuilderT::testTupleT2;
    static constexpr auto jsonFieldsCount = KeyIndexBuilderT::jsonFieldsCount;
    static constexpr auto sortedKeyIndexArrayTemp = KeyIndexBuilderT::sortedKeyIndexArray;

};


}
#endif // CPP_JSON_REFLECTION_HPP
