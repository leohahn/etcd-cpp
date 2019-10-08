cmake_minimum_required(VERSION 3.1)
project(EtcdApi)

#---------------------------
# Dependencies
#---------------------------
# 1. gRPC
find_package(gRPC CONFIG REQUIRED)
# 2. protobuf
find_package(protobuf CONFIG REQUIRED)

#---------------------------
# Protos
#---------------------------
add_library(Protos STATIC
    # Auth
    src/proto/auth.grpc.pb.h
    src/proto/auth.grpc.pb.cc
    src/proto/auth.pb.h
    src/proto/auth.pb.cc
    # EtcdServer
    src/proto/etcdserver.grpc.pb.h
    src/proto/etcdserver.grpc.pb.cc
    src/proto/etcdserver.pb.h
    src/proto/etcdserver.pb.cc
    # KV
    src/proto/kv.grpc.pb.h
    src/proto/kv.grpc.pb.cc
    src/proto/kv.pb.h
    src/proto/kv.pb.cc
    # RPC
    src/proto/rpc.grpc.pb.h
    src/proto/rpc.grpc.pb.cc
    src/proto/rpc.pb.h
    src/proto/rpc.pb.cc)

target_link_libraries(Protos PUBLIC protobuf::libprotobuf-lite)

#---------------------------
# Etcd library target
#---------------------------
add_library(EtcdApi
  src/EtcdApi.cpp)

target_include_directories(EtcdApi
  PUBLIC
    include
  PRIVATE
    src)

target_link_libraries(EtcdApi
  PRIVATE
    # We do not want the gRPC libraries publicly declared
    # since we do not use it in the interface.
    gRPC::gpr
    gRPC::grpc
    gRPC::grpc++
    gRPC::grpc_cronet
    Protos)

#---------------------------
# Setup Tests
#---------------------------
include(CTest)
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(EtcdApiTests
      test/MainTest.cpp)
    
    target_link_libraries(EtcdApiTests
      PRIVATE
        EtcdApi
        GTest::gtest
        GTest::gmock)
endif()