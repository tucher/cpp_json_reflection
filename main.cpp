#include "cpp_json_reflection.hpp"
#include <iostream>

#include <sstream>
#include <list>
using JSONReflection::JS;

using std::vector, std::list, std::array;

struct InnerStruct {
    JS<std::string, "name">  name;
    JS<double,      "value"> value;
};

struct InnerStruct2 {
    JS<std::string, "p1">           p1;
    JS<double,      "p2">           p2 = 42.367;
    JS<double,      "p3">           p5;
    JS<bool,        "p4">           p3;
    JS<InnerStruct, "inner_struct"> p6;
};

struct InnerStruct3 {
    JS<InnerStruct2, "3level_nesting"> value;
};

struct RootObject_ {
    JS<InnerStruct3,            "obj">          obj;
    JS<std::int64_t,            "a">            a;
    std::string                                 UNAVAILABLE_FOR_JSON;
    JS<list<JS<InnerStruct>>,   "values_array"> values;
    JS<InnerStruct2,            "params">       params;
    JS<std::vector<char>,       "string_like">  dummy1;
    JS<std::vector<JS<bool>>,   "primitive_array">  dummy2;
    JS<std::array<JS<std::int64_t>, 5>,   "ints_array">  dummy3;
};

using RootObject = JS<RootObject_>;

static_assert (sizeof(JS<RootObject_>) == sizeof (RootObject_) );

struct InnerFreeObject {
    JS<bool, "flag_1"> f1 = true;
    JS<bool, "flag_2"> f2 = false;
};

struct FreeObject_ {
    JS<InnerFreeObject,              "flags">          flags;
    JS<std::int64_t,                 "counter"  >      counter;
    JS<array<JS<InnerFreeObject>, 3>,"flags_array">   flag_array;
    struct {} dummy;
    struct {} dummy2;
    JS<std::int64_t,                 "counter2"  >      counter2;
    struct {} dummy3;
};

using FreeObject = JS<FreeObject_>;

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

static_assert (std::is_trivially_copyable_v<FreeObject>);

void f1(InnerStruct2 & s) {
    s.p3 = true;
}



int main()
{


    std::string inp(R"(
        {
            "primitive_array": [true,false,true,false],
            "obj": {
                "3level_nesting": {
                    "p1": "deser1",
                    "p2":0,
                    "p3":0,
                    "p4": false,
                    "inner_struct": {
                        "name":"deser2",
                        "value":0
                    }
                }
            },
            "a":123456,
            "values_array":[
                {"name":"deser3","value":-273},
                {"name":"deser4","value":-274}
            ],
            "params":{
                "p1":"",
                "p2":0,
                "p3":0,
                "p4":false,
                "inner_struct": {
                    "name":"deser6","value":0
                }
            }
        }
    )");

    std::string inp3(R"(
        {
            "obj": {
                "3level_nesting": {
                    "p1": "deser1",
                    "p2":0,
                    "p3":0,
                    "p4": false,
                    "inner_struct": {
                        "name":"deser2",
                        "value":0
                    }
                }
            },
            "a":123456,

            "params":{
                "p1":"",
                "p2":0,
                "p3":0,
                "p4":false,
                "inner_struct": {
                    "name":"deser6","value":0
                }
            }
        }
    )");

    RootObject t3;

    bool res1 = t3.Deserialise(inp3.begin(), inp3.end());

    JS<bool, ""> boolV{true};
    boolV = false;
    RootObject testInit{{
            .a {123456},
            .values {{
                    {{.name = {"Fuu"}, .value = {-273}}},
                    {{.name = {"Fuuu"}, .value = {-274}}}
                     }},
            .dummy2 {{true, false, true, false}},
            .dummy3 {{10, 100, 1000, 10000, 100000}}
                        }};

    testInit.Serialize([](const char * d, std::size_t size){
        std::cout << std::string(d, size);
        return true;
    });

    std::cout << std::endl << std::endl;
    std::cout << std::flush;


    struct DeserTestObj {
        JS<bool, "flag_1"> f1 = true;
        JS<bool, "flag_2"> f2 = false;
    };

    JS<DeserTestObj> deserTester;

    std::string inp1(R"(
        {
           "flag_1": false,
            "fuuu": {"f1": [1, 2, 3, 4], "f2":false},
           "flag_2": true
        }
    )");


    bool res = deserTester.Deserialise(inp1.begin(), inp1.end());
    std::cout << "Deserialisation result: " << res << std::endl;

    std::cout << std::endl << std::endl;
    //    t1.a = 42;
    //    auto tt = t1.a;

    //    RootObject t4 = t1;

    InnerStruct ins1{std::string("fu"), 42};

    RootObject t2;
    t2.a = 100500;
    t2.obj.value.p6.name = "hello";
    f1(t2.obj.value);

    t2.values = {{{}, {}, {}}};


    JS<InnerStruct> s;
    s.name = "erfefr";
    s.value = 810;
    t2.values.push_back(s);

    t2.values.push_back({std::string("tybtb"), 811});
    t2.values.push_back({std::string("brbw er"), 814});
    t2.values.push_back({std::string("qweqe"), 900});
    t2.values.push_back({std::string(",[;.["), 100});



    t2.params.p1 = "inner object";
    t2.params.p2 = NAN;
    t2.params.p3 = true;
    t2.params.p5 = 36.6;

    t2.obj.value.p1 = "HELLO";


    t2.Serialize([](const char * d, std::size_t size){
        std::cout << std::string(d, size);
        return true;
    });





    return 0;
}
