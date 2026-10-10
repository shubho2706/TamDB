cmake_minimum_required(VERSION 3.20)
project(tamdb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set Binaries Names
set(TAMDB_LIB "tamdb_lib")
set(TAMDB_EXE "tamdb")
set(TAMDB_RAFT_LIB "tamdb_raft")
set(TAMDB_SERVICE_BIN "tamdb_service_bin")


function(generate_proto PROTO_COMPONENT)
    set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/${PROTO_COMPONENT}.proto)
    set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated PARENT_SCOPE)
    
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

    set(${PROTO_COMPONENT}_PROTO_SRCS ${PROTO_GEN_DIR}/${PROTO_COMPONENT}.pb.cc PARENT_SCOPE) # Needed in caller
    set(PROTO_HDRS ${PROTO_GEN_DIR}/${PROTO_COMPONENT}.pb.h)
    set(${PROTO_COMPONENT}_GRPC_SRCS ${PROTO_GEN_DIR}/${PROTO_COMPONENT}.grpc.pb.cc PARENT_SCOPE) # Needed in caller
    set(GRPC_HDRS ${PROTO_GEN_DIR}/${PROTO_COMPONENT}.grpc.pb.h)

    add_custom_command(
        OUTPUT  ${PROTO_GEN_DIR}/${PROTO_COMPONENT}.pb.cc
                ${PROTO_GEN_DIR}/${PROTO_COMPONENT}.pb.h
                ${PROTO_GEN_DIR}/${PROTO_COMPONENT}.grpc.pb.cc
                ${PROTO_GEN_DIR}/${PROTO_COMPONENT}.grpc.pb.h
        COMMAND $<TARGET_FILE:protobuf::protoc>
        ARGS --cpp_out=${PROTO_GEN_DIR}
             --grpc_out=${PROTO_GEN_DIR}
             -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
             --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
             ${PROTO_FILE}
             DEPENDS ${PROTO_FILE}
             COMMENT "Generating gRPC/protobuf code from tamdb.proto")
endfunction()



option(TAMDB_BUILD_EXE "Build TamDB Exe" OFF)
option(BUILD_SERVICE_LAYER "Build GRPC Interface" ON)
option(BUILD_RAFT_LAYER "Build Raft Layer" ON)
option(TAMDB_BUILD_TESTS "Build tests" OFF)
option(TAMDB_BUILD_BENCHMARKS "Build benchmarks" OFF)

# 1. Engine Library
file(GLOB_RECURSE TAMDB_SOURCES CONFIGURE_DEPENDS
    engine/src/*.cpp
    engine/src/*/*.cpp
)
add_library(${TAMDB_LIB} ${TAMDB_SOURCES})
target_include_directories(${TAMDB_LIB} PUBLIC engine/include)

# 2. Main executable - for local testing 
if(TAMDB_BUILD_EXE)
    add_executable(${TAMDB_EXE} engine/tamdb_lib_test_main.cpp)
    target_link_libraries(${TAMDB_EXE} PRIVATE ${TAMDB_LIB})
endif()

# 3. Build the Raft Layer
if(BUILD_RAFT_LAYER) 
    file(GLOB_RECURSE RAFT_SOURCES CONFIGURE_DEPENDS
        raft/include/*/*.h
        raft/include/*.h
        raft/src/*.cpp)

    add_library(${TAMDB_RAFT_LIB} ${RAFT_SOURCES})

    target_include_directories(${TAMDB_RAFT_LIB} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/raft/include
                ${CMAKE_CURRENT_SOURCE_DIR}/raft/src/)
endif()

# 4. Build the Service Interface Layer
if(BUILD_SERVICE_LAYER)
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
    generate_proto(tamdb)
    generate_proto(raft)
    
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

    add_executable(${TAMDB_SERVICE_BIN} ${TAMDB_SERVICE_SOURCES}
                                        ${tamdb_PROTO_SRCS}
                                        ${tamdb_GRPC_SRCS}
                                        ${raft_PROTO_SRCS}
                                        ${raft_GRPC_SRCS} )
    
    target_compile_options(${TAMDB_SERVICE_BIN} PRIVATE -Wno-deprecated-declarations -Wno-return-type)

    target_include_directories(${TAMDB_SERVICE_BIN} PRIVATE 
                ${PROTO_GEN_DIR} 
                ${CMAKE_CURRENT_SOURCE_DIR}/node/include/)
    target_link_libraries(${TAMDB_SERVICE_BIN} PRIVATE ${TAMDB_LIB} grpc++ 
                            libprotobuf nlohmann_json::nlohmann_json)                

    # Include the link the raft Lib
    target_include_directories(${TAMDB_SERVICE_BIN} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/raft/include)
    target_link_libraries(${TAMDB_SERVICE_BIN} PRIVATE ${TAMDB_RAFT_LIB})
    
endif()

# 5. Build Tests Layer
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
    target_link_libraries(tamdb_tests PRIVATE ${TAMDB_LIB} GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(tamdb_tests)
endif()

# 6. Build Benchmarks Layer
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
    target_link_libraries(tamdb_bench PRIVATE ${TAMDB_LIB} benchmark::benchmark_main)
endif()
