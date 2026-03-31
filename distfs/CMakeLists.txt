cmake_minimum_required(VERSION 3.20)
project(distfs VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# gRPC / Protobuf
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GRPC REQUIRED grpc++)
  pkg_check_modules(GRPCPP REQUIRED grpc++_reflection)
endif()

# Helper to locate grpc_cpp_plugin
if(gRPC_FOUND)
  get_target_property(GRPC_CPP_PLUGIN_EXECUTABLE gRPC::grpc_cpp_plugin LOCATION)
else()
  find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)
endif()

# ------------------------------------------------------------
# Proto generation
# ------------------------------------------------------------
set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(PROTO_SRC "${PROTO_DIR}/distfs.proto")
set(PROTO_OUT "${CMAKE_BINARY_DIR}/proto_gen")
file(MAKE_DIRECTORY "${PROTO_OUT}")

set(GENERATED_PROTO_SRCS
    "${PROTO_OUT}/distfs.pb.cc"
    "${PROTO_OUT}/distfs.pb.h"
    "${PROTO_OUT}/distfs.grpc.pb.cc"
    "${PROTO_OUT}/distfs.grpc.pb.h"
)

add_custom_command(
  OUTPUT ${GENERATED_PROTO_SRCS}
  COMMAND protobuf::protoc
  ARGS
    --grpc_out="${PROTO_OUT}"
    --cpp_out="${PROTO_OUT}"
    -I "${PROTO_DIR}"
    "--plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}"
    "${PROTO_SRC}"
  DEPENDS "${PROTO_SRC}"
  COMMENT "Generating protobuf and gRPC stubs from distfs.proto"
  VERBATIM
)

add_library(distfs_proto STATIC ${GENERATED_PROTO_SRCS})
target_include_directories(distfs_proto PUBLIC "${PROTO_OUT}")
if(gRPC_FOUND)
  target_link_libraries(distfs_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)
else()
  target_link_libraries(distfs_proto PUBLIC ${GRPC_LIBRARIES} ${Protobuf_LIBRARIES})
  target_include_directories(distfs_proto PUBLIC ${GRPC_INCLUDE_DIRS})
endif()

# ------------------------------------------------------------
# Common library (shared across all three binaries)
# ------------------------------------------------------------
add_library(distfs_common STATIC
    common/sha256.cpp
    common/wal.cpp
    common/config.cpp
)
target_include_directories(distfs_common PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/common"
)
target_link_libraries(distfs_common PUBLIC OpenSSL::Crypto Threads::Threads)

# ------------------------------------------------------------
# Raft library
# ------------------------------------------------------------
add_library(distfs_raft STATIC
    raft/raft_log.cpp
    raft/raft_node.cpp
)
target_include_directories(distfs_raft PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(distfs_raft PUBLIC distfs_common distfs_proto)

# ------------------------------------------------------------
# Metadata Server binary
# ------------------------------------------------------------
add_executable(metadata_server
    metadata_server/main.cpp
    metadata_server/metadata_store.cpp
    metadata_server/metadata_service.cpp
)
target_include_directories(metadata_server PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(metadata_server PRIVATE distfs_raft distfs_proto distfs_common)

# ------------------------------------------------------------
# Storage Daemon binary
# ------------------------------------------------------------
add_executable(storage_daemon
    storage_daemon/main.cpp
    storage_daemon/chunk_store.cpp
    storage_daemon/storage_service.cpp
    storage_daemon/heartbeat_client.cpp
)
target_include_directories(storage_daemon PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(storage_daemon PRIVATE distfs_proto distfs_common)

# ------------------------------------------------------------
# CLI binary
# ------------------------------------------------------------
add_executable(distfs-cli
    client/main.cpp
    client/chunker.cpp
    client/uploader.cpp
    client/downloader.cpp
)
target_include_directories(distfs-cli PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(distfs-cli PRIVATE distfs_proto distfs_common)

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------
enable_testing()
find_package(GTest QUIET)

if(GTest_FOUND)
  macro(distfs_test name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}")
    target_link_libraries(${name} PRIVATE distfs_common GTest::gtest_main)
    add_test(NAME ${name} COMMAND ${name})
  endmacro()

  distfs_test(test_sha256  tests/test_sha256.cpp)
  distfs_test(test_wal     tests/test_wal.cpp)
  distfs_test(test_chunker tests/test_chunker.cpp)

  # Raft test also links distfs_raft
  add_executable(test_raft tests/test_raft.cpp)
  target_include_directories(test_raft PRIVATE "${CMAKE_SOURCE_DIR}")
  target_link_libraries(test_raft PRIVATE distfs_raft GTest::gtest_main)
  add_test(NAME test_raft COMMAND test_raft)
else()
  message(WARNING "GTest not found — unit tests will not be built.")
endif()

# ------------------------------------------------------------
# Compiler flags
# ------------------------------------------------------------
add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()
