#include <iostream>
#include "cpp_json_reflection.hpp"

using JSONReflection::JS;

using std::vector, std::list, std::array;

struct InnerStruct {
    JS<std::string, "name">  name;
    JS<double,      "value"> value;
};

struct InnerStruct2 {
    JS<std::string, "p1">           p1;
    JS<double,      "p2">           p2;
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
};

using RootObject = JS<RootObject_>;

static_assert (sizeof(JS<RootObject_>) == sizeof (RootObject_) );

struct InnerFreeObject {
    JS<bool, "flag_1"> f1;
    JS<bool, "flag_2"> f2;
};

struct FreeObject_ {
    JS<InnerFreeObject,              "flags">          flags;
    JS<std::int64_t,                 "counter"  >      counter;
    JS<array<JS<InnerFreeObject>, 3>,"flags_array">   flag_array;
};

using FreeObject = JS<FreeObject_>;

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

    JS<bool, ""> boolV{true};
    boolV = false;
    RootObject testInit{{
        .a {123456},
        .values {{
                       {{.name = {"Fuu"}, .value = {-273}}},
                       {{.name = {"Fuuu"}, .value = {-274}}}
                   }}
    }};

    testInit.Serialize(std::cout);
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


    t2.Serialize(std::cout);


    return 0;
}
