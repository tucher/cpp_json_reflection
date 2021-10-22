#include "cpp_json_reflection.hpp"
#include <iostream>

#include <sstream>
#include <list>
#include <chrono>
#include <utility>


template<typename F, typename... Args>
void doPerformanceTest(std::string title, std::size_t count, F func, Args&&... args){
    std::chrono::high_resolution_clock::time_point t1=std::chrono::high_resolution_clock::now();
    for(std::size_t i = 0; i < count; i++) {
        func(std::forward<Args>(args)...);
    }
    double toUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()-t1).count();
    double res = toUs / double(count);
    std::cerr << title << " " << res << " us" << std::endl << std::flush;
}

using JSONReflection::JS;

using std::vector, std::list, std::array, std::string, std::int64_t;

struct InnerStruct {
    JS<string, "name">  name;
    JS<double, "value"> value;
};

struct InnerStruct2 {
    JS<array<char, 15>, "p1">           p1;
    JS<double,          "p2">           p2 = 42.367;
    JS<double,          "p3">           p3;
    JS<bool,            "p4">           p4;
    JS<InnerStruct,     "inner_struct"> inner_struct;
};

struct InnerStruct3 {
    JS<InnerStruct2, "3level_nesting"> _3level_nesting;
};

struct RootObject_ {
    JS<InnerStruct3,            "obj">          obj;
    JS<int64_t,                 "a">            a;
    string                                      UNAVAILABLE_FOR_JSON;
    JS<list<JS<InnerStruct>>,   "values_array"> values_array;
    JS<InnerStruct2,            "params">       params;
    JS<vector<char>,            "string_like">  string_like;
    JS<vector<JS<bool>>,        "primitive_array">  primitive_array;
    JS<array<JS<int64_t>, 5>,   "ints_array">  ints_array;
};

using RootObject = JS<RootObject_>;




int main()
{
    constexpr char inp3const[]{R"(
        {
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

    RootObject t3;
    char * b = &inp3.data()[0];
    char * e = &inp3.data()[inp3.size()];

    doPerformanceTest("Perf Test For t3.Deserialize: ", 2000000, [&t3, &b, &e]{
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

    doPerformanceTest("Perf Test For t3.Serialize: ", 1000000, [&testInit, &counter]{
        counter = 0;
        testInit.Serialize([&counter](const char * d, std::size_t size){
            counter += size;
            return true;
        });
    });

    testInit.Serialize([](const char * d, std::size_t size){
        std::cout << std::string(d, size);
        return true;
    });

    std::cout << std::endl << std::endl;
    std::cout << std::flush;
    std::cout << std::endl << std::endl;


    InnerStruct ins1{std::string("fu"), 42};

    JS<InnerStruct> s;
    s.name = "erfefr";
    s.value = 810;

    RootObject t2;
    t2.a = 100500;
    t2.obj._3level_nesting.inner_struct.name = "hello";

    t2.values_array = {{{}, {}, {}}};

    t2.values_array.push_back(s);

    t2.values_array.push_back({std::string("tybtb"), 811});
    t2.values_array.push_back({std::string("brbw er"), 814});
    t2.values_array.push_back({std::string("qweqe"), 900});
    t2.values_array.push_back({std::string(",[;.["), 100});

    t2.params.p1 = array<char, 15> {{'i', 'n', 'n', 'e', 'r', 's', 't', 'r', 0}};
    t2.params.p2 = NAN;
    t2.params.p4 = true;
    t2.params.p3 = 36.6;

    t2.obj._3level_nesting.p1 = array<char, 15> {{'H', 'E', 'L', 'L', 'O', 0}};

    t2.Serialize([](const char * d, std::size_t size){
        std::cout << std::string(d, size);
        return true;
    });

    return 0;
}



static_assert (sizeof(JS<RootObject_>) == sizeof (RootObject_) );


struct InnerFreeObject {
    JS<bool, "flag_1"> f1 = true;
    JS<bool, "flag_2"> f2 = false;
};

struct FreeObject_ {
    JS<InnerFreeObject,              "flags">          flags;
    JS<int64_t,                      "counter"  >      counter;
    JS<array<JS<InnerFreeObject>, 3>,"flags_array">    flag_array;
    struct {} dummy;
    struct {} dummy2;
    JS<int64_t,                      "counter2"  >      counter2;
    struct {} dummy3;
};
using FreeObject = JS<FreeObject_>;



static_assert (std::is_trivially_copyable_v<FreeObject>);


static_assert (std::tuple_size_v< FreeObject::testTupleT> == 7);
//static_assert (std::tuple_element_t<1, FreeObject::testTupleT>)
static_assert (std::same_as<JS<std::int64_t, "counter"  >, std::tuple_element_t<1, FreeObject::testTupleT2>> );
static_assert (std::tuple_element_t<1, FreeObject::testTupleT>::skip == false );
static_assert (std::tuple_element_t<2, FreeObject::testTupleT>::FieldName.Length == 11 );
static_assert (std::tuple_element_t<3, FreeObject::testTupleT>::skip == true );
static_assert (FreeObject::jsonFieldsCount == 4 );
static_assert (FreeObject::sortedKeyIndexArrayTemp.size() == 4 );


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

static_assert (swl::visit(indexGetter, FreeObject::sortedKeyIndexArrayTemp[3]) == 2 );
static_assert (swl::visit(keyLengthGetter, FreeObject::sortedKeyIndexArrayTemp[3]) == 11 );


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
    JS<bool> boolV{true};
    boolV = false;

    JS<bool> boolV2 = true;

    RootObject t1;

    t1.a = 42;
    auto tt = t1.a;

    RootObject t4 = t1;


    RootObject t2;
    t2.a = 100500;
    t2.obj._3level_nesting.inner_struct.name = "hello";

    t2.values_array = {{{}, {}, {}}};


    f1(t2.obj._3level_nesting);

}
