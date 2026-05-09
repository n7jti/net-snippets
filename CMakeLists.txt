cmake_minimum_required(VERSION 3.20)

project(net-snippets LANGUAGES C CXX)

if(NOT WIN32)
  message(WARNING "This repository contains Windows console samples. Build on Windows to produce executables.")
  return()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(interface-enum src/interface_enum.c)
target_compile_definitions(interface-enum PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(interface-enum PRIVATE iphlpapi ws2_32 shell32)

add_executable(echo-server src/echo_server.cpp)
target_compile_definitions(echo-server PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(echo-server PRIVATE ws2_32 shell32)
