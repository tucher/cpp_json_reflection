#include "cpp_json_reflection.hpp"
#include <iostream>

#include <sstream>
#include <list>
#include <chrono>
#include <utility>
#include <map>
#include "test_utils.hpp"
using JSONReflection::J;

using std::vector, std::list, std::array, std::string, std::int64_t, std::map;

namespace TestFeatures {
struct InnerStruct {
    J<string, "name">  name;
    J<double, "value"> value;
    auto operator<=>(const InnerStruct&) const = default;
};

struct InnerStruct2 {
    J<array<char, 15>, "p1">           p1;
    J<double,          "p2">           p2 = 42.367;
    J<double,          "p3">           p3;
    J<bool,            "p4">           p4;
    J<InnerStruct,     "inner_struct"> inner_struct;

    auto operator<=>(const InnerStruct2&) const = default;
};

struct InnerStruct3 {
    J<InnerStruct2, "3level_nesting"> _3level_nesting;
    auto operator<=>(const InnerStruct3&) const = default;
};

struct CustomDateString {
    auto operator<=>(const CustomDateString&) const = default;
    bool JSONSerialize( JSONReflection::SerializerOutputCallbackConcept auto && clb) const {
        char v[] = "2021-10-\n24T11:25:29Z";
        return clb(v, sizeof(v)-1);
    }

    using DeserializeContainerT = string;
    static_assert (JSONReflection::StringOutputContainerConcept<DeserializeContainerT>);
    bool JSONDeserialise(DeserializeContainerT & cont) {
        char v[] = "2021-10\\n-24T11:25:29Z";
        auto r = memcmp(v, & *cont.begin(), sizeof(v)-1);

        return r == 0;
    }
};

using JSONMapTestType = map<string, J<bool>>;

struct RootObject_ {
    J<CustomDateString,        "date">        date;
    J<InnerStruct3,            "obj">          obj;
    J<int64_t,                 "a">            a;
    string                                      UNAVAILABLE_FOR_JSON;
    J<list<J<InnerStruct>>,   "values_array"> values_array;
    J<InnerStruct2,            "params">       params;
    J<vector<char>,            "string_like">  string_like;
    J<vector<J<bool>>,        "primitive_array">  primitive_array;
    J<array<J<int64_t>, 5>,   "ints_array">  ints_array;

    J<array<J<InnerStruct3>, 2>,            "obj">          fixed_sized_objs;
    J<map<string, J<bool>>, "flag_map"> flag_map;
    auto operator<=>(const RootObject_&) const = default;
};

using RootObject = J<RootObject_>;
}

//using TestInline = J
int canadaJsonPerfTest();
int twitterJsonPerfTest();


int main()
{
//    return twitterJsonPerfTest();
//    return canadaJsonPerfTest();
    constexpr char inp3const[]{R"(
        {
            "skip_me2" :  "..  .",
            "skip_me3": {"ints_array": [-0, -0.4, -235.12323e+2, -8, 0.001e-3, 0, 10], "string_like": "i am std::vector, not std::string", "obj": { "3level_nesting": { "p1": "deser1", "p2":-373, "p3":-378, "p4": false, "inner_struct": { "name":"deser2", "value":-100 } } }, "a":123456, "params":{ "p1":"string value", "p2":1000, "p3":2000, "p4":false, "inner_struct": { "name":"deser6","value":800 } } },

            "flag_map": {"f1": true, "f2": false},
            "date":    "2021-10\n-24T11:25:29Z"
            "primitive_array": [true,false,true,false],
            "ints_array": [-2, -4, -6, -8, -10],
            "string_like": "i am std::vector, not std::string",
            "skip_me": { "primitive_array": [true,false,true,false], "ints_array": [-2, -4, -6, -8, -10], "string_like": "i am std::vector, not std::string", "obj": { "3level_nesting": { "p1": "deser1", "p2":-373, "p3":-378, "p4": false, "inner_struct": { "name":"deser2", "value":-100 } } }, "a":123456, "params":{ "p1":"string value", "p2":1000, "p3":2000, "p4":false, "inner_struct": { "name":"deser6","value":800 } } },
            "obj": {
                "3level_nesting": {
                    "p1": "deser1",
                    "p2":-373,
                    "p3":-378,
                    "p4": false,
                    "inner_struct": {
                        "name":"deser2",
                        "value":-100
                    }
                }
            },
            "a":123456,

            "params":{
                "p1":"string value",
                "p2":1000.0001,
                "p3":2000.0002,
                "p4":false,
                "inner_struct": {
                    "name":"deser6","value":-8.2e20
                }
            }
        }
    )"};
    std::string inp3 = inp3const;
    std::string inp3copy = inp3;
    std::cout << "JSON length: " << inp3.size() << std::endl;

    TestFeatures::RootObject t3;
    char * b = &inp3.data()[0];
    char * e = &inp3.data()[inp3.size()];

    t3.Deserialize(inp3);
    t3.Deserialize(inp3.cbegin(), inp3.cend());
    if (inp3 != inp3copy) {
        throw 1;
    }
    doPerformanceTest("Perf Test For t3.Deserialize", 2000000, [&t3, &b, &e]{
        bool res1 = t3.Deserialize(b, e);
        if(!res1) throw 1;
    });

//    while(true) {
//        bool res1 = t3.Deserialize(b, e);
//        if(!res1) throw 1;

//    }
    {
        string out;
        t3.Serialize(out);
        std::cout << std::endl << "Size ↑: " << out.size() << std::endl;
    }

    TestFeatures::RootObject testInit{{
            .a {4444},
            .values_array {{
                    {{.name = {"Fuu"}, .value = {-273.55556}}},
                    {{.name = {"Fuuu"}, .value = {-274.123456789}}}
                     }},
            .primitive_array {{true, false, true, false}},
            .ints_array {{10, 100, 1000, 10000, 100000}}
                        }};

    std::size_t counter = 0;

//    while(true) {
//        counter = 0;
//        testInit.Serialize([&counter](const char * d, std::size_t size){
//            counter += size;
//            return true;
//        });

//    }

    doPerformanceTest("Perf Test For t3.Serialize", 2000000, [&testInit, &counter]{
        counter = 0;
        testInit.Serialize([&counter](const char * d, std::size_t size){
            counter += size;
            return true;
        });
    });

    {
        std::size_t counter = 0;
        testInit.Serialize([&counter](const char * d, std::size_t size){
            std::cout << std::string(d, size);
            counter += size;
            return true;
        });
        std::cout << std::endl << "Size ↑: " << counter << std::endl;
    }

    std::cout << std::endl << std::endl;
    std::cout << std::flush;
    std::cout << std::endl << std::endl;



    return 0;
}

static_assert (sizeof(J<TestFeatures::RootObject_>) == sizeof (TestFeatures::RootObject_) );

namespace FreeObj {
struct InnerFreeObject {
    J<bool, "flag_1"> f1 = true;
    J<bool, "flag_2"> f2 = false;
};

struct FreeObject_ {
    J<InnerFreeObject,              "flags">          flags;
    J<int64_t,                      "counter"  >      counter;
    J<array<J<InnerFreeObject>, 3>,"flags_array">    flag_array;
    struct {} dummy;
    struct {} dummy2;
    J<int64_t,                      "counter2"  >      counter2;
    struct {} dummy3;
};
using FreeObject = J<FreeObject_>;

}


FreeObj::FreeObject compileTimeInit{{
        {{ .f1{true}, .f2{true} }},
        {0},
        {{{
                    {{ .f1{true}, .f2{true} }},
                    {{ .f1{true}, .f2{true} }},
                    {{ .f1{true}, .f2{true} }}
                }}}
                           }};


void f1(TestFeatures::InnerStruct2 & s) {
    s.p3 = true;
}


void compileTimeTests() {
    J<bool> boolV{true};
    boolV = false;

    J<bool> boolV2 = true;

    TestFeatures::RootObject t1;

    t1.a = 42;
    auto tt = t1.a;

    TestFeatures::RootObject t4 = t1;


    TestFeatures::RootObject t22;
    t22.a = 100500;
    t22.obj._3level_nesting.inner_struct.name = "hello";

    t22.values_array = {{}, {}, {}};

    t22.fixed_sized_objs = {
        TestFeatures::InnerStruct3{},
        TestFeatures::InnerStruct3{}
    };
    f1(t22.obj._3level_nesting);



    TestFeatures::InnerStruct ins1{std::string("fu"), 42};

    J<TestFeatures::InnerStruct> s;
    s.name = "erfefr";
    s.value = 810;

    TestFeatures::RootObject t2;
    t2.a = 100500;
    t2.obj._3level_nesting.inner_struct.name = "hello";

    t2.values_array = {{}, {}, {}};

    t2.values_array.push_back(s);

    t2.values_array.push_back({{std::string("tybtb"), 811}});
    t2.values_array.push_back({{std::string("brbw er"), 814}});
    t2.values_array.push_back({{std::string("qweqe"), 900}});
    t2.values_array.push_back({{std::string(",[;.["), 100}});

    t2.params.p1 = array<char, 15> {{'i', 'n', 'n', 'e', 'r', 's', 't', 'r', 0}};
    t2.params.p2 = NAN;
    t2.params.p4 = true;
    t2.params.p3 = 36.6;

    t2.obj._3level_nesting.p1 = array<char, 15> {{'H', 'E', 'L', 'L', 'O', 0}};

    t2.Serialize([](const char * d, std::size_t size){
        std::cout << std::string(d, size);
        return true;
    });

    t2.values_array.push_back(TestFeatures::InnerStruct{});
//    RootObject a1;
//    RootObject_ a2;
//    a2 = a1;
//    a1 = a2;
//    bool vmp = (a1 == a1);
//    bool vmp2 = (a1 == a2);

    list<TestFeatures::InnerStruct> l;
    l.push_back(J<TestFeatures::InnerStruct>{});
    J<int64_t> Jplain1;
    Jplain1 ++;

    auto opsChecker = []<class T>(T plain1) {
        J<T> Jplain1;
        bool b;
        Jplain1 = plain1;
        plain1 = Jplain1;
        Jplain1 = Jplain1;
        J<T> i1(Jplain1);
        J<T> i2(plain1);
        T i3(Jplain1);


        b = plain1 > Jplain1;
        b = Jplain1 > plain1;
        b = Jplain1 > Jplain1;

        b = plain1 < Jplain1;
        b = Jplain1 < plain1;
        b = Jplain1 < Jplain1;

        b = plain1 <= Jplain1;
        b = Jplain1 <= plain1;
        b = Jplain1 <= Jplain1;

        b = plain1 >= Jplain1;
        b = Jplain1 >= plain1;
        b = Jplain1 >= Jplain1;

        b = plain1 == Jplain1;
        b = Jplain1 == plain1;
        b = Jplain1 == Jplain1;

        b = plain1 != Jplain1;
        b = Jplain1 != plain1;
        b = Jplain1 != Jplain1;
    };

    opsChecker(int64_t{});

    opsChecker(TestFeatures::RootObject_{});
    opsChecker(list<J<TestFeatures::InnerStruct>>{});


    static_assert (JSONReflection::d::JSONBasicValue<TestFeatures::CustomDateString>);

    static_assert (JSONReflection::d::JSONMapValue<TestFeatures::JSONMapTestType>);

    static_assert (JSONReflection::StringOutputContainerConcept<std::array<char, 20>>);
    static_assert (JSONReflection::StringOutputContainerConcept<std::vector<char>>);
    static_assert (JSONReflection::StringOutputContainerConcept<std::list<char>>);

    static_assert (JSONReflection::d::StaticContainerTypeConcept<std::array<char, 20>>);

    map<string, TestFeatures::RootObject_> m;
    m.try_emplace("fu");
}
/*
template <JSONReflection::d::ConstString Str>
struct Member {
    static constexpr auto FieldName =  Str;
};

template <std::size_t Index, class T>
struct KeyIndexEntry{
    static constexpr bool skip = false;
    static constexpr auto FieldName =  T::FieldName;
    static constexpr std::size_t OriginalIndex = Index;
};

namespace std{
template <class ... FieldTypes, std::size_t ... Is>
constexpr void swap(mpark::variant<KeyIndexEntry<Is, FieldTypes>...>& lhs, mpark::variant<KeyIndexEntry<Is, FieldTypes>...>& rhs ){
    auto t = lhs;
    lhs = rhs;
    rhs = t;
}
}
template <class T, class I> struct KeyIndexBuilder;
template <class ... FieldTypes, std::size_t ... Is>
struct KeyIndexBuilder<std::tuple<FieldTypes...>, std::index_sequence<Is...>>  {
    using VarT = mpark::variant<KeyIndexEntry<Is, FieldTypes>...>;
    using KeyIndexEntryArrayType = std::array<VarT, sizeof... (Is)>;
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
            return mpark::visit(visitor{}, val);
        }
    };
    static constexpr KeyIndexEntryArrayType sortKeyIndexes(std::array<VarT, sizeof... (Is)>  vals) {
//        KeyIndexEntryArrayType ret;
//        std::size_t j = 0;
//        for(std::size_t i = 0; i < vals.size(); i++) {
//            if(swl::visit([](auto v){ return !v.skip; }, vals[i])) {
//                ret[j] = vals[i];
//                j++;
//            }
//        }
//        std::ranges::sort(ret, std::ranges::less{}, ProjKeyIndexVariantToStringView{});
        std::ranges::sort(vals, std::ranges::less{}, ProjKeyIndexVariantToStringView{});

        return vals;
    }
    static constexpr KeyIndexEntryArrayType sortedKeyIndexArray = sortKeyIndexes({KeyIndexEntry<Is, FieldTypes>()...});
};

int checher2() {

    struct Tst1_ {
        Member< "created_at"> created_at;
        Member< "id">        id;
        Member< "id_str">    id_str;
        Member< "text">        text;
        Member< "source">        source;
        Member< "truncated">        truncated;
        Member< "in_reply_to_status_id">        in_reply_to_status_id;
        Member< "in_reply_to_status_id_str">        in_reply_to_status_id_str;
        Member< "in_reply_to_user_id">        in_reply_to_user_id;
        Member< "in_reply_to_user_id_str">        in_reply_to_user_id_str;
        Member< "in_reply_to_screen_name">        in_reply_to_screen_name;
        Member< "quoted_status_id">        quoted_status_id;
        Member< "quoted_status_id_str">        quoted_status_id_str;
        Member< "is_quote_status">        is_quote_status;
        Member< "quote_count">        quote_count;
        Member< "reply_count">        reply_count;
        Member< "retweet_count">        retweet_count;
        Member< "favorite_count">        favorite_count;
        Member< "favorited">        favorited;
        Member< "retweeted">        retweeted;
        Member< "possibly_sensitive">        possibly_sensitive;
        Member< "filter_level">        filter_level;
        Member< "lang">        lang;

    };
    using KeyIndexBuilderT = KeyIndexBuilder<decltype (pfr::structure_to_tuple(std::declval<Tst1_>())), std::make_index_sequence<pfr::tuple_size_v<Tst1_>>>;
    static constexpr auto sortedKeyIndexArray = KeyIndexBuilderT::sortedKeyIndexArray;
    constexpr KeyIndexBuilderT _temp1;
    constexpr KeyIndexBuilderT::KeyIndexEntryArrayType unsortedKeyEntries;

    return 1;
}
bool v = checher2();
*/
