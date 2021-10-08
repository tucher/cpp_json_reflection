# Another JSON library
## Main feature
Maps structures to/from JSON without macros and code generation
## Usage

### Defining structures

- Need to wrap all fields into JS<> template

        struct InnerStruct {
            JS<std::string, "label">  label;
            JS<double,      "value">  value;
        };

        struct InnerStruct2 {
            JS<std::string, "p1">           p1;
            JS<double,      "p2">           p2;
            JS<double,      "p3">           p5;
            JS<bool,        "p4">           p3;
            JS<InnerStruct, "inner_struct"> p6;
        };

        struct InnerStruct3 {
            JS<InnerStruct2, "nested"> value;
        };

        struct RootObject_ {
            JS<InnerStruct3,            "obj">          obj;
            JS<std::int64_t,            "a">            a;
            JS<list<JS<InnerStruct>>,   "values_array"> values;
            JS<InnerStruct2,            "params">       params;
        };

        using RootObject = JS<RootObject_>;

- Structs are usable in the usual way:

        RootObject root;
        root.obj.value.p6.name = "I am deeply nested";

        void f1(InnerStruct2 & s) {
            s.p3 = true;
        }
        f1(root.obj.value)

- To use JSON array, use this technique:

        using ArrayFieldT = JS<ContainerT<T>>;
    Where T is type already wrapped into JS<> and ContainerT is std::list, std::vector or something compatible.

- Initialisation:

        RootObject testInit{{
            .a {123456},
            .values {{
                       {{.name = {"Fuu"}, .value = {-273} }},
                       {{.name = {"Fuuu"}, .value = {-274} }}
            }}
        }};
    Addititinal {} pair is needed.
### Serialisation:
    
    root.Serialize(std::cout);