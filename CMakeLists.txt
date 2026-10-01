cmake_minimum_required(VERSION 3.20)
project(tamdb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TAMDB_BUILD_TESTS "Build tests" ON)
option(TAMDB_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(BUILD_GRPC_LAYER "Build GRPC Interface" ON)
option(BUILD_RAFT_LAYER "Build Raft Layer" OFF)

file(GLOB_RECURSE TAMDB_SOURCES CONFIGURE_DEPENDS
    engine/src/*.cpp
    engine/src/*/*.cpp
)

# Exclude entry points and the gRPC layer from library sources
# list(FILTER TAMDB_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
# list(FILTER TAMDB_SOURCES EXCLUDE REGEX "src/server_main\\.cpp$")
# list(FILTER TAMDB_SOURCES EXCLUDE REGEX "src/service/")

# Library
add_library(tamdb_lib ${TAMDB_SOURCES})
target_include_directories(tamdb_lib PUBLIC engine/include)

# Main executable
add_executable(tamdb engine/tamdb_lib_test_main.cpp)
target_link_libraries(tamdb PRIVATE tamdb_lib)

if(BUILD_RAFT_LAYER) 
 file(GLOB_RECURSE RAFT_SOURCES CONFIGURE_DEPENDS
        raft/src/*.cpp
        node/src/*/*.cpp)

    add_executable(raft ${RAFT_SOURCES})

    target_include_directories(raft PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
                ${CMAKE_CURRENT_SOURCE_DIR}/raft/src/)
    target_link_libraries(raft PRIVATE ) #TODO
endif()

if(BUILD_GRPC_LAYER)
    set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
    set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(utf8_range_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(
        grpc
        GIT_REPOSITORY https://github.com/grpc/grpc.git
        GIT_TAG v1.83.1 
    )
    FetchContent_MakeAvailable(grpc)

    # --- generate protobuf + gRPC C++ from the .proto ---
    set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/tamdb.proto)
    set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

    set(PROTO_SRCS ${PROTO_GEN_DIR}/tamdb.pb.cc)
    set(PROTO_HDRS ${PROTO_GEN_DIR}/tamdb.pb.h)
    set(GRPC_SRCS  ${PROTO_GEN_DIR}/tamdb.grpc.pb.cc)
    set(GRPC_HDRS  ${PROTO_GEN_DIR}/tamdb.grpc.pb.h)

    add_custom_command(
        OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
        COMMAND $<TARGET_FILE:protobuf::protoc>
        ARGS --cpp_out=${PROTO_GEN_DIR}
             --grpc_out=${PROTO_GEN_DIR}
             -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
             --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
             ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating gRPC/protobuf code from tamdb.proto")

    
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(json)

    # --- server target: entry point + service glue + generated code ---
    file(GLOB_RECURSE TAMDB_SERVICE_SOURCES CONFIGURE_DEPENDS
        node/src/*.cpp
        node/src/*/*.cpp)

    add_executable(tamdb_node
        ${TAMDB_SERVICE_SOURCES}
        ${PROTO_SRCS} ${GRPC_SRCS})

    target_include_directories(tamdb_node PRIVATE 
                ${PROTO_GEN_DIR} 
                ${CMAKE_CURRENT_SOURCE_DIR}/node/include
                ${CMAKE_CURRENT_SOURCE_DIR}/node/src/)
    target_link_libraries(tamdb_node PRIVATE tamdb_lib grpc++ libprotobuf nlohmann_json::nlohmann_json)
endif()

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
        engine/tests/*.cpp
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
