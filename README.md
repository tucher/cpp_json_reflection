# Another JSON library
## Main feature
High-performance mapping between native first-class C++ objects and JSON without macros and code generation.

## Usage
All you need is to define C++ structures to model data. 
For example, there is GeoJSON data, [canada.json](https://github.com/boostorg/json/blob/develop/bench/data/canada.json):

    #include <cpp_json_reflection.hpp>
    #include <vector>
    #include <array>
    #include <string>
    #include <fstream>

    namespace Geo {
    using std::vector, std::array, std::string

    using JSONReflection::J;

    using Point = array<J<double>, 2>;
    
    using PolygonRing = vector<J<Point>>;
    
    using PolygonCoordinates = vector<J<PolygonRing>>;
    
    struct Geometry {
        J<string,             "type">        type;
        J<PolygonCoordinates, "coordinates"> coordinates;
    };

    struct Properties {
        J<string, "name"> name;
    };

    struct Feature {
        J<string,     "type">       type;
        J<Properties, "properties"> properties;
        J<Geometry,   "geometry">   geometry;
    };

    struct Root_ {
        J<string,             "type">      type;
        J<vector<J<Feature>>, "features" > features;
    };

    using Root = J<Root_>;
    }
    int main() {
        std::ifstream ifs("canada.json");
        string data(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        
        Geo::Root root;

        if(root.Deserialize(data)) {
            //all done!
        }

        string output;
        if(root.Serialize(output)) {
            //all done!
        }
    }

So:
- JSON object are plain C++ structs. Fields order doesn't matter for successful parsing, excess JSON data is silently skipped. Fields order is preserved in serialized JSON output.
- JSON array is stl-compatible container, including fixed-sized, like ```Point = std::array<T, 2>```, which models  geo coordinates in our example. Heterogenous JSON arrays are not supported.
- JSON plain value is ```bool```, ```double```, ```std::int64_t``` or string-like. String-like means stl-compatible [contigeous container](https://en.cppreference.com/w/cpp/named_req/ContiguousContainer) with ```char```s, including fixed-sized, like ```std::array<char, 20>```. 
- JSON ```null``` value doesn't make much sense if we are in strongly-typed world, and is not supported.

The only difference between usual structures and JSON-ready is that you need to wrap all JSON-related objects into ```J<>``` template. If field inside structure models JSON object key-value pair, add key string literal after field's type.

With such ```J``` wrapper, objects are *(almost)* usable in usual way:

    Root root;
    root.features[0].geometry.type = "I am deeply nested";
    
    Geometry geom;
    root.features[0].geometry = geom;

or

    void ffuu(Properties & s) {
        s.name = "ffuu";
    }
    f1(root.features[1].properties);

or 

    Root initialisedRoot {{
        .type{"some type"},
        .features {{
            {{
                .type{"f1"}
            }},
            {{
                .type{"f1"}
            }}
        }}
    }}

Object may contain garbage half-parsed data if deserialization fails, so use something like this to restore previous values:
    
    if(auto temp = obj; !obj.Deserialize(DataContainer)) {
        obj = temp
    } else {
        //success;
    }

## Performance

- No dymamic memory used by the library itself, so it is possible to use it in completely heap-free environment. Until all strings and arrays are modelled with fixed-sized containers, of course. There are more low-level, zero-copy interfaces to Serialize/Deserialize for such operation mode.

- No memory overhead:

        static_assert (sizeof(J<Root_>) == sizeof (Root_));

    Key strings for user type T are ```static constexpr``` and stored inside ```J<T>``` type

- What about speed, on my MacbookPro 2014 laptop it is about 500 megabytes/s for canada.json parsing and 200 for serializing when compiled with GCC 11. For Twitter data (commonly used for benchmarks) it is about 500mb/s for read and write. There is a comparison chart [here](http://vinniefalco.github.io/doc/json/json/benchmarks.html#json.benchmarks.parse_numbers_json), so we are somewhere near the middle-top.

- And let's don't talk about binary size and compilation time:) More tests needed, but for relatively small models it looks "ok".

## Options, features

- ```J```-wrapped JSON-ready types could be declared in shorter form, if needed. It could be useful for nested arrays:

        J<list<
            J<list<
                ....
                J<array<
                    J<double>,
                2>>
                ...
            >>
        >> deeplyNestedArrays;
- It is fine to have non-static fields inside ```J```-wrapped structs which model JSON objects:
        
        struct Obj_ {
            int a;
            J<bool, "flag"> flag;
        };
        using Obj = J<Obj_>;

    Such fields are just ignored. 

- To work with JSON object in form of (string -> YOURTYPE), use map stl-compatible container, like:

        using std::map;
        struct Root_ {
            J<map<string, J<YOURTYPE>>, "map"> map;
        }
        using Root = J<Root_>;

- Custom string parsing (to implement date support, for example). Add a pair of serialize/deserialize methods to your class, then use it with  ```J``` wrapper, as usual:

        struct CustomDateString {

            bool SerializeInternal( JSONReflection::SerializerOutputCallbackConcept auto && clb) const {
                char v[] = "2021-10-24T11:25:29Z";
                return clb(v, sizeof(v)-1);
            }

            template<class InpIter> 
                requires JSONReflection::InputIteratorConcept<InpIter>
            bool DeserialiseInternal(InpIter begin, InpIter end) {
                char v[] = "2021-10-24T11:25:29Z";
                return 0 == memcmp(v, & *begin, sizeof(v)-1);
            }
        };
        struct Root_ {
            J<CustomDateString, "date">      type;
            ...
        };
        using Root = J<Root_>;



## Dependencies

- C++ 20 is mandatory, I've developed and tested the library with GCC11
- Amazing [PFR](https://github.com/boostorg/pfr) library, which inspired me and get us all closer to C++ introspection 
- Better replacement for ```std::variant```, [swl variant](https://github.com/groundswellaudio/swl-variant)
- High-performance double to string convertion code from [simdjson](https://github.com/simdjson/simdjson/blob/master/src/to_chars.cpp)
- High-performance string to double convertion library, [fast_double_parser](https://github.com/lemire/fast_double_parser)

## TODO
- Compile-time options for max size if dynamic objects, querying preallocated containers size, etc..
- Complete test suit, including fuzzing
- Option to fail on excess keys
- String escape/unescape
- Recursive structures?
- Prettyfying?
- refine string concept
- outputEscapedString should be done in iterators, like other things
- nulled flag
- option for strict size/max size fixed container filling
