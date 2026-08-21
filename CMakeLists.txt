cmake_minimum_required(VERSION 3.20)
project(tamdb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TAMDB_BUILD_TESTS "Build tests" OFF)
option(TAMDB_BUILD_BENCHMARKS "Build benchmarks" OFF)

# Library
add_library(tamdb_lib
    src/distance.cpp
    src/flat_index.cpp
)
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

    add_executable(tamdb_tests
        tests/distance_test.cpp
        tests/flat_index_test.cpp
    )
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

    add_executable(tamdb_bench
        benchmarks/distance_bench.cpp
    )
    target_link_libraries(tamdb_bench PRIVATE tamdb_lib benchmark::benchmark_main)
endif()
