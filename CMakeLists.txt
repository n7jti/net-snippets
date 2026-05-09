cmake_minimum_required(VERSION 3.20)

project(net-snippets LANGUAGES C CXX)

if(NOT WIN32)
  message(WARNING "This repository contains Windows console samples. Build on Windows to produce executables.")
  return()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(src/interface-enum)
add_subdirectory(src/echo-server)
