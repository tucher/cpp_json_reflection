cmake_minimum_required(VERSION 3.14)  # FetchContent requires 3.11+, using 3.14 for better support

project(JsonFusion VERSION 1.1.0 LANGUAGES CXX C)

# Option to enable C++26 reflection mode
option(JSONFUSION_CPP26_REFLECTION "Enable C++26 reflection mode (excludes glaze, adds -freflection)" OFF)

if(JSONFUSION_CPP26_REFLECTION)
    set(CMAKE_CXX_STANDARD 26)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -freflection")
    message(STATUS "JsonFusion: C++26 reflection mode enabled")
else()
    set(CMAKE_CXX_STANDARD 23)
    message(STATUS "JsonFusion: C++23 mode (default)")
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fetch external dependencies for benchmarking
include(FetchContent)

# Fetch RapidJSON
FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG        master  # or use a specific tag like v1.1.0
  GIT_SHALLOW    TRUE
)

# RapidJSON is header-only, just make it available
set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(rapidjson)

# Fetch reflect-cpp
FetchContent_Declare(
  reflectcpp
  GIT_REPOSITORY https://github.com/getml/reflect-cpp.git
  GIT_TAG        v0.21.0  # Use a specific stable version
  GIT_SHALLOW    TRUE     # Only download the latest commit for faster fetch
)

# Configure reflect-cpp with JSON support only (minimal dependencies)
set(REFLECTCPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(REFLECTCPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(reflectcpp)

# Fetch Glaze (only in C++23 mode - glaze doesn't support -freflection yet)
if(NOT JSONFUSION_CPP26_REFLECTION)
    FetchContent_Declare(
      glaze
      GIT_REPOSITORY https://github.com/stephenberry/glaze.git
      GIT_TAG        main  # Use main branch for latest
      GIT_SHALLOW    TRUE
    )

    # Configure Glaze (header-only, minimal config)
    set(glaze_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(glaze)
endif()

# Single include directory containing both JsonFusion/ and pfr/
include_directories(${PROJECT_SOURCE_DIR}/include)


FetchContent_Declare(
    yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG        master   # or a specific tag like v0.10.0
)

FetchContent_MakeAvailable(yyjson)
set(YYJSON_SRC_DIR "${yyjson_SOURCE_DIR}/src")


file(GLOB JSON_FUSION
     ${PROJECT_SOURCE_DIR}/include/JsonFusion/*.hpp
)

# add_executable(consteval_tests
#     tests/consteval_tests.cpp
#     ${JSON_FUSION}
# )
# target_link_libraries(consteval_tests PRIVATE yyjson)

# add_executable(sax_demo
#     tests/sax_demo.cpp
#     ${JSON_FUSION}
# )

# YyJSON WireSink integration example
# add_executable(yyjson_wiresink_example
#     examples/yyjson_wiresink.cpp
#     ${JSON_FUSION}
# )
# target_link_libraries(yyjson_wiresink_example PRIVATE yyjson)


# add_executable(migration_example
#     examples/schema_evolution_bool_to_enum.cpp
#     ${JSON_FUSION}
# )
# target_link_libraries(migration_example PRIVATE yyjson)

# Canada JSON benchmark
set(CANADA_SOURCES
    benchmarks/canada_json/canada_json_parsing.cpp
    benchmarks/canada_json/canada_json_parsing.hpp
    benchmarks/canada_json/canada_json_parsing_rapidjson.cpp
    ${JSON_FUSION}
)

# Add glaze sources only in C++23 mode
if(NOT JSONFUSION_CPP26_REFLECTION)
    list(APPEND CANADA_SOURCES benchmarks/canada_json/canada_json_parsing_glaze.cpp)
endif()

add_executable(canada_json_parsing ${CANADA_SOURCES})

target_compile_options(canada_json_parsing PRIVATE
  $<$<CONFIG:Release>:
    -fno-exceptions
    -fno-rtti
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
      -flto=8
      -ffunction-sections
      -fdata-sections
    >
  >
)

target_link_options(canada_json_parsing PRIVATE
  $<$<CONFIG:Release>:
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-flto=8>
    # GNU ld / lld
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-Wl,--gc-sections>
    $<$<AND:$<CXX_COMPILER_ID:Clang>,$<PLATFORM_ID:Linux>>:-Wl,--gc-sections>
    # Apple ld
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
  >
)

target_include_directories(canada_json_parsing PRIVATE
  ${rapidjson_SOURCE_DIR}/include
)

if(JSONFUSION_CPP26_REFLECTION)
    target_compile_definitions(canada_json_parsing PRIVATE JSONFUSION_BENCHMARK_NO_GLAZE)
    target_link_libraries(canada_json_parsing PRIVATE yyjson)
else()
    target_link_libraries(canada_json_parsing PRIVATE glaze::glaze yyjson)
endif()


# Synthetic benchmarks
add_executable(benchmarks
    benchmarks/syntetic/main.cpp
    benchmarks/syntetic/bench_matrix.hpp
    benchmarks/syntetic/benchmarks_jsonfusion_tu.hpp
    benchmarks/syntetic/benchmarks_rapidjson_tu.cpp
    benchmarks/syntetic/benchmarks_rapidjson_tu.hpp
    benchmarks/syntetic/benchmarks_models.hpp
    ${JSON_FUSION}
)

target_include_directories(benchmarks PRIVATE
  ${rapidjson_SOURCE_DIR}/include
)

target_compile_options(benchmarks PRIVATE
  $<$<CONFIG:Release>:
    -fno-exceptions
    -fno-rtti
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
      -flto=8
      -ffunction-sections
      -fdata-sections
    >
  >
)

target_link_options(benchmarks PRIVATE
  $<$<CONFIG:Release>:
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-flto=8>
    # GNU ld / lld
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-Wl,--gc-sections>
    $<$<AND:$<CXX_COMPILER_ID:Clang>,$<PLATFORM_ID:Linux>>:-Wl,--gc-sections>
    # Apple ld
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
  >
)

target_link_libraries(benchmarks PRIVATE yyjson)


# Twitter JSON benchmark
set(TWITTER_SOURCES
    benchmarks/twitter_json/twitter_model_generic.hpp
    benchmarks/twitter_json/twitter_json_parsing.cpp
    benchmarks/twitter_json/rapidjson_populate.hpp
    benchmarks/twitter_json/twitter_json_parsing_rapidjson.cpp
    benchmarks/twitter_json/reflectcpp_parsing.hpp
    benchmarks/twitter_json/twitter_json_parsing_reflectcpp.cpp
    benchmarks/twitter_json/benchmark.hpp
    benchmarks/twitter_json/twiiter_json_parsing_yyjson.cpp
    ${JSON_FUSION}
)

# Add glaze sources only in C++23 mode
if(NOT JSONFUSION_CPP26_REFLECTION)
    list(APPEND TWITTER_SOURCES 
        benchmarks/twitter_json/glaze_parsing.hpp
        benchmarks/twitter_json/twitter_json_parsing_glaze.cpp
    )
endif()

add_executable(twitter_json_parsing ${TWITTER_SOURCES})

target_include_directories(twitter_json_parsing PRIVATE
  ${rapidjson_SOURCE_DIR}/include
)

target_compile_options(twitter_json_parsing PRIVATE
  $<$<CONFIG:Release>:
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
      -flto=8
      -ffunction-sections
      -fdata-sections
    >
  >
)

target_link_options(twitter_json_parsing PRIVATE
  $<$<CONFIG:Release>:
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-flto=8>
    # GNU ld / lld
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-Wl,--gc-sections>
    $<$<AND:$<CXX_COMPILER_ID:Clang>,$<PLATFORM_ID:Linux>>:-Wl,--gc-sections>
    # Apple ld
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
  >
)

if(JSONFUSION_CPP26_REFLECTION)
    target_compile_definitions(twitter_json_parsing PRIVATE JSONFUSION_BENCHMARK_NO_GLAZE)
    target_link_libraries(twitter_json_parsing PRIVATE reflectcpp yyjson)
else()
    target_link_libraries(twitter_json_parsing PRIVATE reflectcpp glaze::glaze yyjson)
endif()


add_executable(fp_test
    tests/fp/main.cpp
    ${JSON_FUSION}
)
