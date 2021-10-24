#include "cpp_json_reflection.hpp"
#include <list>
#include "test_utils.hpp"

#include "fstream"
#include <string.h>

using JSONReflection::J;
using std::vector, std::list, std::array, std::string, std::int64_t;


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


J<list<
    J<list<
        J<array<
            J<double>,
        2>>
    >>
>, "coordinates"> coordinates;
using T = decltype(coordinates);

static_assert (JSONReflection::JSONWrappedValue<T>);
static_assert (std::is_same_v<JSONReflection::JSONValueKindEnumArray, T::JSONValueKind>);
static_assert (JSONReflection::JSONWrappedValue<T::ItemType>);
static_assert (JSONReflection::JSONWrappedValue<T::ItemType::ItemType>);
static_assert (JSONReflection::JSONWrappedValue<T::ItemType::ItemType::ItemType>);
static_assert (std::is_same_v<JSONReflection::JSONValueKindEnumPlain, T::ItemType::ItemType::ItemType::JSONValueKind>);

//static_assert (std::is_trivially_assignable_v<Root, Root_>);
//static_assert (std::is_assignable_v<Root, Root_>);

//static_assert (std::is_trivially_assignable_v<Root_, Root>);


int canadaJsonPerfTest() {

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
        }};

    constexpr char inpconst[]{R"(
        {
            "type": "FeatureCollection",
            "features": [
                {
                    "type": "Feature",
                    "properties": {
                        "name": "Canada"
                    },
                    "geometry": {
                        "type": "Polygon",
                        "coordinates": [
                           [
                               [
                                   -65.613616999999977,
                                   43.420273000000009
                               ],
                               [
                                   -65.619720000000029,
                                   43.418052999999986
                               ]
                           ],
                            [
                                [
                                    -65.0,
                                    43.0
                                ],
                                [
                                    -65.5,
                                    43.5
                                ]
                            ]
                        ]
                    }
                }
            ]
        }
    )"};
    std::string inp = inpconst;

    std::ifstream ifs("../cpp_json_reflection/canada.json");
     inp = std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());

    std::cout << "inp.size " << inp.size() << std::endl;
    char * b = &inp.data()[0];
    char * e = &inp.data()[inp.size()];
    Root root;
    bool res;

    res = root.Deserialize(b, e);
    if(!res) throw 1;

//    while(true) {
//        bool res1 = root.Deserialize(b, e);
//        if(!res1) throw 1;

//    }

    {
//        std::size_t counter = 0;
//        while(true) {
//            counter = 0;
//            root.Serialize([&counter](const char * d, std::size_t size){
//                counter += size;
//                return true;
//            });

//        }
    }

   {
        std::size_t counter = 0;
        root.Serialize([&counter](const char * d, std::size_t size){
            counter += size;
            return true;
        });
        std::cout << "Serialized size: " << counter << std::endl;
   }




    std::size_t counter = 0;
    auto outputSimulator = new char[10000000];
    std::size_t outputPtr = 0;
    auto serializingCallback = [&counter, &outputPtr, &outputSimulator](const char * d, std::size_t size){
        memcpy(outputSimulator + outputPtr, d, size);
        outputPtr += size;
        counter += size;
        return true;
    };
    doPerformanceTest("canada.json  serializing", 100, [&root, &counter, &outputPtr, &serializingCallback]{
        counter = 0;
        outputPtr = 0;
        root.Serialize(serializingCallback);
    });
    std::ofstream outputFile("./serialised_output.json");
    outputFile.write(outputSimulator, outputPtr);
    outputFile.close();


    doPerformanceTest("canada.json parsing", 100, [&res, &root, &b, &e]{
        res = root.Deserialize(b, e);
//        root.Serialize([](const char * d, std::size_t size){
//            std::cout << std::string(d, size);
//            return true;
//        });

        if(!res) throw 1;
    });
    auto & feat = root.features.front();

    root.features[0].geometry.type = "I am deeply nested";
    string s("wefwef");
    s += root.features[0].geometry.type;
    return 0;

}
