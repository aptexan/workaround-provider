# Copyright 2018 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# cmake build file for C++ tofu-plugin example.
# Assumes protobuf and gRPC have been installed using cmake.
# See cmake_externalproject/CMakeLists.txt for all-in-one cmake build
# that automatically builds all the dependencies before building ${proto_stem}.

cmake_minimum_required(VERSION 3.22)

project(tofu-plugin-cpp C CXX)

include(cmake/common.cmake)

# Require C++23 (overrides the C++17 default set in common.cmake)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# OpenTofu provider protocol (tfplugin5)
get_filename_component(proto_abs_path "proto/tfplugin5.proto" ABSOLUTE)
get_filename_component(proto_path "${proto_abs_path}" PATH)
get_filename_component(proto_stem "${proto_abs_path}" NAME_WE)

# Generated sources
set(proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/${proto_stem}.pb.cc")
set(proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${proto_stem}.pb.h")
set(grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/${proto_stem}.grpc.pb.cc")
set(grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${proto_stem}.grpc.pb.h")
add_custom_command(
      OUTPUT "${proto_srcs}" "${proto_hdrs}" "${grpc_srcs}" "${grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${proto_abs_path}"
      DEPENDS "${proto_abs_path}")

# go-plugin controller service (lets Core ask the plugin to shut down)
set(ctrl_srcs "${CMAKE_CURRENT_BINARY_DIR}/grpc_controller.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/grpc_controller.grpc.pb.cc")
set(ctrl_hdrs "${CMAKE_CURRENT_BINARY_DIR}/grpc_controller.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/grpc_controller.grpc.pb.h")
get_filename_component(ctrl_abs_path "proto/grpc_controller.proto" ABSOLUTE)
add_custom_command(
      OUTPUT ${ctrl_srcs} ${ctrl_hdrs}
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${ctrl_abs_path}"
      DEPENDS "${ctrl_abs_path}")

# go-plugin stdio service (stream of the plugin's stdout/stderr to Core)
set(stdio_srcs "${CMAKE_CURRENT_BINARY_DIR}/grpc_stdio.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/grpc_stdio.grpc.pb.cc")
set(stdio_hdrs "${CMAKE_CURRENT_BINARY_DIR}/grpc_stdio.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/grpc_stdio.grpc.pb.h")
get_filename_component(stdio_abs_path "proto/grpc_stdio.proto" ABSOLUTE)
add_custom_command(
      OUTPUT ${stdio_srcs} ${stdio_hdrs}
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${stdio_abs_path}"
      DEPENDS "${stdio_abs_path}")

# go-plugin broker service (stream for negotiating extra connections)
set(broker_srcs "${CMAKE_CURRENT_BINARY_DIR}/grpc_broker.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/grpc_broker.grpc.pb.cc")
set(broker_hdrs "${CMAKE_CURRENT_BINARY_DIR}/grpc_broker.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/grpc_broker.grpc.pb.h")
get_filename_component(broker_abs_path "proto/grpc_broker.proto" ABSOLUTE)
add_custom_command(
      OUTPUT ${broker_srcs} ${broker_hdrs}
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${broker_abs_path}"
      DEPENDS "${broker_abs_path}")

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# plugin_grpc_proto
add_library(plugin_grpc_proto
  ${ctrl_srcs}
  ${ctrl_hdrs}
  ${stdio_srcs}
  ${stdio_hdrs}
  ${broker_srcs}
  ${broker_hdrs}
  ${grpc_srcs}
  ${grpc_hdrs}
  ${proto_srcs}
  ${proto_hdrs})
target_link_libraries(plugin_grpc_proto
  absl::check
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

  ## Tartget name (executable file) is same as the project name (tofu-plugin-cpp)
  set (plugin_exe_name "terraform-provider-workaround")
  add_executable(${plugin_exe_name} "src/tofu-plugin.cpp")
  target_link_libraries(${plugin_exe_name}
    plugin_grpc_proto
    absl::check
    absl::flags
    absl::flags_parse
    absl::log
    absl::log_initialize
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF})
