cmake_minimum_required(VERSION 3.20)

project(
  AsyncDataLoader
  VERSION 0.1.0
  DESCRIPTION "Bounded-memory async read-process-write preprocessing pipeline"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(asyncdataloader_stage0 src/main.cpp)
target_compile_features(asyncdataloader_stage0 PRIVATE cxx_std_20)

enable_testing()

add_test(
  NAME stage0_smoke
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:asyncdataloader_stage0>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage0_smoke_test.cmake
)
