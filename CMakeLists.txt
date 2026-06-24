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

find_package(Threads REQUIRED)

add_executable(asyncdataloader_stage0 src/main.cpp)
target_compile_features(asyncdataloader_stage0 PRIVATE cxx_std_20)

add_executable(stage1_simple_task_demo examples/coroutines/simple_task_demo.cpp)
target_compile_features(stage1_simple_task_demo PRIVATE cxx_std_20)

add_executable(stage1_manual_resume_demo examples/coroutines/manual_resume_demo.cpp)
target_compile_features(stage1_manual_resume_demo PRIVATE cxx_std_20)

add_executable(stage1_delay_awaiter_demo examples/coroutines/delay_awaiter_demo.cpp)
target_compile_features(stage1_delay_awaiter_demo PRIVATE cxx_std_20)
target_link_libraries(stage1_delay_awaiter_demo PRIVATE Threads::Threads)

add_executable(stage2_fd_guard_test
  src/util/fd_guard.cpp
  tests/stage2_fd_guard_test.cpp
)
target_compile_features(stage2_fd_guard_test PRIVATE cxx_std_20)
target_include_directories(stage2_fd_guard_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(stage2_file_io_test
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  tests/stage2_file_io_test.cpp
)
target_compile_features(stage2_file_io_test PRIVATE cxx_std_20)
target_include_directories(stage2_file_io_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(stage2_sync_preprocess_baseline_test
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/baseline/sync_preprocess_baseline.cpp
  tests/stage2_sync_preprocess_baseline_test.cpp
)
target_compile_features(stage2_sync_preprocess_baseline_test PRIVATE cxx_std_20)
target_include_directories(stage2_sync_preprocess_baseline_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(stage2_timer_test
  src/util/timer.cpp
  tests/stage2_timer_test.cpp
)
target_compile_features(stage2_timer_test PRIVATE cxx_std_20)
target_include_directories(stage2_timer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(stage2_sync_preprocess_demo
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/util/timer.cpp
  src/baseline/sync_preprocess_baseline.cpp
  examples/stage2_sync_preprocess_demo.cpp
)
target_compile_features(stage2_sync_preprocess_demo PRIVATE cxx_std_20)
target_include_directories(stage2_sync_preprocess_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_testing()

add_test(
  NAME stage0_smoke
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:asyncdataloader_stage0>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage0_smoke_test.cmake
)

add_test(
  NAME stage1_simple_task_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage1_simple_task_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage1_simple_task_demo_test.cmake
)

add_test(
  NAME stage1_manual_resume_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage1_manual_resume_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage1_manual_resume_demo_test.cmake
)

add_test(
  NAME stage1_delay_awaiter_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage1_delay_awaiter_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage1_delay_awaiter_demo_test.cmake
)

add_test(NAME stage2_fd_guard COMMAND stage2_fd_guard_test)
add_test(NAME stage2_file_io COMMAND stage2_file_io_test)
add_test(NAME stage2_sync_preprocess_baseline COMMAND stage2_sync_preprocess_baseline_test)
add_test(NAME stage2_timer COMMAND stage2_timer_test)

add_test(
  NAME stage2_sync_preprocess_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage2_sync_preprocess_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage2_sync_preprocess_demo_test.cmake
)
