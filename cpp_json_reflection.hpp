#ifndef CPP_JSON_REFLECTION_HPP
#define CPP_JSON_REFLECTION_HPP

#include "pfr.hpp"
#include <list>
#include <string>
#include <type_traits>
#include <concepts>
#include <iterator>
#include <ranges>
#include <cmath>

namespace JSONReflection {


template <typename CharT, std::size_t N> struct JSONFieldNameStringLiteral
{
    constexpr JSONFieldNameStringLiteral(const CharT (&foo)[N+1]) { std::copy_n(foo, N+1, m_data); }
    CharT m_data[N+1];
    static constexpr std::size_t Length = N;
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


template<typename T>
concept JSONBasicValue = std::same_as<T, bool> || std::same_as<T, double>|| std::same_as<T, std::int64_t> || std::same_as<T, std::string>;

template<typename T>
concept JSONArrayValue = std::ranges::range<T> && !std::same_as<T, std::string> && JSONWrappedValue<typename T::value_type>;

template<typename T>
concept JSONObjectValue = std::is_class_v<T> && !std::ranges::range<T> && !std::same_as<T, std::string>;



template <JSONFieldNameStringLiteral Str = "">
struct A {
    static constexpr auto str = Str;
};

using hello_A = A<"hello">;
static_assert (decltype(hello_A::str)::Length == 5);

using hello_A2 = A<>;
static_assert (decltype(hello_A2::str)::Length == 0);



static_assert (std::ranges::range<std::list<int>>);


template <class Src, JSONFieldNameStringLiteral Str = "">
requires JSONBasicValue<Src> || JSONArrayValue<Src> || JSONObjectValue<Src>
class JS {
};

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

    void Serialize(std::ostream & output) const {
        if constexpr(std::same_as<bool, Src>) {
            if(content) output << "true";
            else output << "false";
        } else if constexpr(std::same_as<std::string, Src>) {
            output << '"'<<content<<'"';
        } else if constexpr(std::same_as<double, Src>) {
            if(std::isnan(content)) {
                output << "0";
            } else {
                output << content;
            }
        } else if constexpr(std::same_as<std::int64_t, Src>) {
            output << content;
        }
    }
};

template <JSONArrayValue Src, JSONFieldNameStringLiteral Str>
class JS<Src, Str> : public Src{
public:
    static constexpr auto FieldName = Str;
    using JSONValueKind = JSONValueKindEnumArray;
    void Serialize(std::ostream & output) const {
        output << "[";
        std::size_t last = static_cast<Src>(*this).size()-1;
        std::size_t i = 0;
        for(const auto & item: static_cast<Src>(*this)) {
            item.Serialize(output);
            if(i != last) {
                output << ",";
            }
            i++;
        }
        output << "]";
    }
};

template <JSONObjectValue Src, JSONFieldNameStringLiteral Str>
class JS<Src, Str> : public Src {
public:
    using JSONValueKind = JSONValueKindEnumObject;
    static constexpr auto FieldName = Str;

    void Serialize(std::ostream & output) const {
        std::size_t last_index = 0;
        pfr::for_each_field(static_cast<Src>(*this), [&last_index](const auto & val, std::size_t index){
            using T = std::decay_t<decltype (val)>;
            if constexpr(JSONWrappedValue<T>) { last_index = index;}
        });
        output << "{";
        auto visitor = [&output, last_index](const auto & val, std::size_t index) {
            using T = std::decay_t<decltype (val)>;
            if constexpr(JSONWrappedValue<T>) {
                output << '"' << std::string(val.FieldName.m_data, val.FieldName.Length) << "\":";
                val.Serialize(output);
                if(index != last_index) {
                    output << ",";
                }
            }
        };
        pfr::for_each_field(static_cast<Src>(*this), visitor);
        output << "}";
    }
};

}
#endif // CPP_JSON_REFLECTION_HPP
