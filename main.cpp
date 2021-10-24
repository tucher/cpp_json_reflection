#include "cpp_json_reflection.hpp"
#include <iostream>

#include <sstream>
#include <list>
#include <chrono>
#include <utility>

#include "test_utils.hpp"

using JSONReflection::J;

using std::vector, std::list, std::array, std::string, std::int64_t;

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
    bool SerializeInternal( JSONReflection::SerializerOutputCallbackConcept auto && clb) const {
        char v[] = "2021-10-24T11:25:29Z";
        return clb(v, sizeof(v)-1);
    }

    template<class InpIter> requires JSONReflection::InputIteratorConcept<InpIter>
    bool DeserialiseInternal(InpIter begin, InpIter end) {
        char v[] = "2021-10-24T11:25:29Z";
        auto r = memcmp(v, & *begin, sizeof(v)-1);

        return r == 0;
    }
};


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

    auto operator<=>(const RootObject_&) const = default;
};

using RootObject = J<RootObject_>;


//using TestInline = J
int canadaJsonPerfTest();
int main()
{
    return canadaJsonPerfTest();
    constexpr char inp3const[]{R"(
        {
            "date":    "2021-10-24T11:25:29Z"
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
    std::cout << "JSON length: " << inp3.size() << std::endl;

    RootObject t3;
    char * b = &inp3.data()[0];
    char * e = &inp3.data()[inp3.size()];

    t3.Deserialize(inp3);
    doPerformanceTest("Perf Test For t3.Deserialize", 2000000, [&t3, &b, &e]{
        bool res1 = t3.Deserialize(b, e);
        if(!res1) throw 1;
    });

//    while(true) {
//        bool res1 = t3.Deserialize(b, e);
//        if(!res1) throw 1;

//    }

//    t3.Deserialize(inp3);
    RootObject testInit{{
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



static_assert (sizeof(J<RootObject_>) == sizeof (RootObject_) );


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



static_assert (std::is_trivially_copyable_v<FreeObject>);


//static_assert (std::tuple_size_v< FreeObject::testTupleT> == 7);
////static_assert (std::tuple_element_t<1, FreeObject::testTupleT>)
//static_assert (std::same_as<J<std::int64_t, "counter"  >, std::tuple_element_t<1, FreeObject::testTupleT2>> );
//static_assert (std::tuple_element_t<1, FreeObject::testTupleT>::skip == false );
//static_assert (std::tuple_element_t<2, FreeObject::testTupleT>::FieldName.Length == 11 );
//static_assert (std::tuple_element_t<3, FreeObject::testTupleT>::skip == true );
//static_assert (FreeObject::jsonFieldsCount == 4 );
//static_assert (FreeObject::sortedKeyIndexArrayTemp.size() == 4 );


static constexpr auto indexGetter = []<class KeyIndexT>(KeyIndexT v) -> std::size_t {
        if constexpr (KeyIndexT::skip == false) {
            return v.OriginalIndex;
          }
          else {
              return -1;
          }

    };

static constexpr auto keyLengthGetter = []<class KeyIndexT>(KeyIndexT v) -> std::size_t {
        if constexpr (KeyIndexT::skip == false) {
            return v.FieldName.Length;
          }
          else {
              return -1;
          }

    };

//static_assert (swl::visit(indexGetter, FreeObject::sortedKeyIndexArrayTemp[3]) == 2 );
//static_assert (swl::visit(keyLengthGetter, FreeObject::sortedKeyIndexArrayTemp[3]) == 11 );


FreeObject compileTimeInit{{
        {{ .f1{true}, .f2{true} }},
        {0},
        {{{
                    {{ .f1{true}, .f2{true} }},
                    {{ .f1{true}, .f2{true} }},
                    {{ .f1{true}, .f2{true} }}
                }}}
                           }};


void f1(InnerStruct2 & s) {
    s.p3 = true;
}


void compileTimeTests() {
    J<bool> boolV{true};
    boolV = false;

    J<bool> boolV2 = true;

    RootObject t1;

    t1.a = 42;
    auto tt = t1.a;

    RootObject t4 = t1;


    RootObject t22;
    t22.a = 100500;
    t22.obj._3level_nesting.inner_struct.name = "hello";

    t22.values_array = {{}, {}, {}};

    t22.fixed_sized_objs = {
        InnerStruct3{},
        InnerStruct3{}
    };
    f1(t22.obj._3level_nesting);



    InnerStruct ins1{std::string("fu"), 42};

    J<InnerStruct> s;
    s.name = "erfefr";
    s.value = 810;

    RootObject t2;
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

    t2.values_array.push_back(InnerStruct{});
//    RootObject a1;
//    RootObject_ a2;
//    a2 = a1;
//    a1 = a2;
//    bool vmp = (a1 == a1);
//    bool vmp2 = (a1 == a2);

    list<InnerStruct> l;
    l.push_back(J<InnerStruct>{});
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

    opsChecker(RootObject_{});
    opsChecker(list<J<InnerStruct>>{});


    static_assert (JSONReflection::d::JSONBasicValue<CustomDateString>);
}
