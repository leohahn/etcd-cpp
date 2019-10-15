cmake_minimum_required(VERSION 3.5.1)
project(EtcdApi)

#---------------------------
# fmt
#---------------------------
add_subdirectory(src/vendor/fmt)

#---------------------------
# Etcd library target
#---------------------------
add_library(EtcdApi
    include/Etcd.hpp
    src/Etcd.cpp
    # Protos ======
    # Auth
    src/proto/auth.pb.h
    src/proto/auth.pb.cc
    src/proto/auth.grpc.pb.h
    src/proto/auth.grpc.pb.cc
    # EtcdServer
    src/proto/etcdserver.pb.h
    src/proto/etcdserver.pb.cc
    src/proto/etcdserver.grpc.pb.h
    src/proto/etcdserver.grpc.pb.cc
    # KV
    src/proto/kv.pb.h
    src/proto/kv.pb.cc
    src/proto/kv.grpc.pb.h
    src/proto/kv.grpc.pb.cc
    # RPC
    src/proto/rpc.grpc.pb.h
    src/proto/rpc.grpc.pb.cc
    src/proto/rpc.pb.h
    src/proto/rpc.pb.cc)

target_include_directories(EtcdApi
  PUBLIC
    include
  PRIVATE
    src)

#---------------------------
# gRPC
#---------------------------
find_package(gRPC CONFIG REQUIRED)

target_link_libraries(EtcdApi
  PRIVATE
    # We do not want the gRPC libraries publicly declared
    # since we do not use it in the interface.
    gRPC::gpr
    gRPC::grpc
    gRPC::grpc++
    gRPC::grpc_cronet)

if(WIN32)
    # This define is necessary in order to compile with gRPC
    target_compile_definitions(EtcdApi PUBLIC _WIN32_WINNT=0x600)
endif()

target_link_libraries(EtcdApi PRIVATE fmt::fmt)

set_target_properties(EtcdApi PROPERTIES CXX_STANDARD 11)

#---------------------------
# Setup Tests
#---------------------------
include(CTest)
if(BUILD_TESTING)
    find_package(GTest 1.10.0 CONFIG REQUIRED)

    add_executable(EtcdApiTests
      test/MainTest.cpp
      test/LeaseTest.cpp
      test/KVTest.cpp
      test/WatchTest.cpp)

    target_link_libraries(EtcdApiTests
      PRIVATE
        EtcdApi
        GTest::gtest)

  set_target_properties(EtcdApiTests PROPERTIES CXX_STANDARD 11)
endif()
