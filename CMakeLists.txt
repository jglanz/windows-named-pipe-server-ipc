cmake_minimum_required(VERSION 3.30)
project(node_ipc_native_interop)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

cmake_policy(SET CMP0091 NEW)
set(
  CMAKE_MSVC_RUNTIME_LIBRARY
  # Statically link the C++ runtime libraries, but partially override this below
  "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
add_link_options(
  "/DEFAULTLIB:ucrt$<$<CONFIG:Debug>:d>.lib" # include the dynamic UCRT
  "/NODEFAULTLIB:libucrt$<$<CONFIG:Debug>:d>.lib" # ignore the static UCRT
)


find_package(Boost REQUIRED COMPONENTS system)
find_package(gRPC REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

file(GLOB_RECURSE basicSrcFiles src/basic/*.cpp src/basic/*.h)
file(GLOB_RECURSE asyncSrcFiles src/async/*.cpp src/async/*.h)


# Original executables
add_executable(node_ipc_basic ${basicSrcFiles})
target_link_libraries(node_ipc_basic Boost::system)

add_executable(node_ipc_async ${asyncSrcFiles})
target_link_libraries(node_ipc_async Boost::system spdlog::spdlog magic_enum::magic_enum)

add_executable(node_ipc_native_interop_multi src/multithread/multithreaded-pipe-server.cpp)
target_link_libraries(node_ipc_native_interop_multi Boost::system)

# Generate protobuf and gRPC files
set(PROTO_PATH "${CMAKE_CURRENT_LIST_DIR}/src/grpc/proto")
set(PROTO_FILES "${PROTO_PATH}/service.proto")


# Generate protobuf and gRPC files
#protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
#get_filename_component(PROTO_SRC_DIR "${PROTO_SRCS}" PATH)

add_library(nodejs-named-pipe-proto ${PROTO_FILES})
target_link_libraries(nodejs-named-pipe-proto
  PUBLIC
  protobuf::libprotobuf
  gRPC::grpc
  gRPC::grpc++
  gRPC::grpc++_reflection
)

# generated .h files will be placed in the build directory, so we need to add that to the include path
target_include_directories(nodejs-named-pipe-proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET nodejs-named-pipe-proto LANGUAGE cpp
  PROTOC_OPTIONS --experimental_editions)
protobuf_generate(TARGET nodejs-named-pipe-proto LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}"
  PROTOC_OPTIONS --experimental_editions)

#protobuf_generate_grpc_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_FILES})

# New gRPC executables
add_executable(grpc_pipe_server 
    src/grpc/grpc_server.cpp

#    ${PROTO_SRCS} ${PROTO_HDRS}
#    ${GRPC_SRCS} ${GRPC_HDRS}
)

target_link_libraries(grpc_pipe_server PRIVATE nodejs-named-pipe-proto)

add_executable(grpc_pipe_client
    src/grpc/grpc_client.cpp
    ${PROTO_SRCS} ${PROTO_HDRS}
    ${GRPC_SRCS} ${GRPC_HDRS}
)

target_link_libraries(grpc_pipe_client PRIVATE nodejs-named-pipe-proto)
target_include_directories(grpc_pipe_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/grpc ${CMAKE_CURRENT_BINARY_DIR}/src/grpc/proto ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(grpc_pipe_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/grpc ${CMAKE_CURRENT_BINARY_DIR}/src/grpc/proto ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(grpc_pipe_server PRIVATE
    gRPC::grpc++
    gRPC::grpc++_reflection
    protobuf::libprotobuf
)

target_link_libraries(grpc_pipe_client PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
)