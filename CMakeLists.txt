cmake_minimum_required(VERSION 3.20)
project(tamdb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TAMDB_BUILD_TESTS "Build tests" OFF)
option(TAMDB_BUILD_BENCHMARKS "Build benchmarks" OFF)

file(GLOB_RECURSE TAMDB_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*/*.cpp
)

# Exclude main.cpp from library sources
list(FILTER TAMDB_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

# Library
add_library(tamdb_lib ${TAMDB_SOURCES}
    src/hnsw/hnsw_index.cpp)
target_include_directories(tamdb_lib PUBLIC include)

# Main executable
add_executable(tamdb src/main.cpp)
target_link_libraries(tamdb PRIVATE tamdb_lib)

# Tests
if(TAMDB_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    enable_testing()

    file(GLOB_RECURSE TAMDB_TEST_SOURCES CONFIGURE_DEPENDS
        tests/*.cpp
    )

    add_executable(tamdb_tests ${TAMDB_TEST_SOURCES})
    target_link_libraries(tamdb_tests PRIVATE tamdb_lib GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(tamdb_tests)
endif()

# Benchmarks
if(TAMDB_BUILD_BENCHMARKS)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )
    set(BENCHMARK_ENABLE_TESTING OFF)
    FetchContent_MakeAvailable(benchmark)

    file(GLOB_RECURSE TAMDB_BENCH_SOURCES CONFIGURE_DEPENDS
        benchmarks/*.cpp
    )

    add_executable(tamdb_bench ${TAMDB_BENCH_SOURCES})
    target_link_libraries(tamdb_bench PRIVATE tamdb_lib benchmark::benchmark_main)
endif()
