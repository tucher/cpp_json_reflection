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

    int main() {
        std::ifstream ifs("canada.json");
        string data(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        
        Root root;

        if(root.Deserialize(data)) {
            //all done!
        }

        root.Serialize([](const char * d, std::size_t size){
            //data is feeded there;
            return true;
        });
    }

So:
- JSON object are plain C++ structs. Fields order doesn't matter for successful parsing, excess JSON data is silently skipped.
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

- No dymamic memory used by the library itself, so it is possible to use it in completely heap-free environment. Until all strings and arrays are modelled with fixed-sized containers, of course.

- No memory overhead:

        static_assert (sizeof(J<Root_>) == sizeof (Root_));

    Key strings are ```static constexpr``` and stored inside ```J<T, "key">``` type

- What about speed, on my MacbookPro 2014 laptop it is about 300 megabytes/s for canada.json. There is a comparison chart [here](http://vinniefalco.github.io/doc/json/json/benchmarks.html#json.benchmarks.parse_numbers_json), so we are faster then **nlohman**, and relatively the same as **rapidjson**.

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


## Dependencies

- C++ 20 is mandatory, I've developed and tested the library with GCC11
- Amazing [PFR](https://github.com/boostorg/pfr) library, which inspired me and get us all closer to C++ introspection 
- Better replacement for ```std::variant```, [swl variant](https://github.com/groundswellaudio/swl-variant)
- High-performance double to string convertion code from [simdjson](https://github.com/simdjson/simdjson/blob/master/src/to_chars.cpp)
- High-performance string to double convertion library, [fast_double_parser](https://github.com/lemire/fast_double_parser)

## TODO
- Work on complete transparency of ```J```-wrapped objects
- Compile-time options for max size if dynamic objects, preallocated containers size, etc..
- Complete test suit, including fuzzing