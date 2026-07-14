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
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED IMPORTED_TARGET liburing)

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

add_executable(stage3_bench_sync
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/util/timer.cpp
  src/baseline/sync_preprocess_baseline.cpp
  benchmark/stage3_bench_sync.cpp
)
target_compile_features(stage3_bench_sync PRIVATE cxx_std_20)
target_include_directories(stage3_bench_sync PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(stage3_bench_mmap
  src/util/timer.cpp
  benchmark/stage3_bench_mmap.cpp
)
target_compile_features(stage3_bench_mmap PRIVATE cxx_std_20)
target_include_directories(stage3_bench_mmap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(asyncdataloader_benchmark
  src/benchmark/benchmark_stats.cpp
  src/benchmark/benchmark_report.cpp
)
target_compile_features(asyncdataloader_benchmark PUBLIC cxx_std_20)
target_include_directories(asyncdataloader_benchmark PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(stage3_bench_sync PRIVATE asyncdataloader_benchmark)
target_link_libraries(stage3_bench_mmap PRIVATE asyncdataloader_benchmark)

add_executable(stage3_bench_pread
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/util/timer.cpp
  benchmark/stage3_bench_pread.cpp
)
target_compile_features(stage3_bench_pread PRIVATE cxx_std_20)
target_include_directories(stage3_bench_pread PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(stage3_bench_pread PRIVATE asyncdataloader_benchmark)

add_executable(stage3_benchmark_stats_test
  tests/stage3_benchmark_stats_test.cpp
)
target_link_libraries(stage3_benchmark_stats_test PRIVATE asyncdataloader_benchmark)

add_executable(stage4_uring_read_once
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  examples/io_uring/uring_read_once.cpp
)
target_compile_features(stage4_uring_read_once PRIVATE cxx_std_20)
target_include_directories(stage4_uring_read_once PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(stage4_uring_read_once PRIVATE PkgConfig::LIBURING)

add_executable(stage4_uring_batch_read
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  examples/io_uring/uring_batch_read.cpp
)
target_compile_features(stage4_uring_batch_read PRIVATE cxx_std_20)
target_include_directories(stage4_uring_batch_read PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(stage4_uring_batch_read PRIVATE PkgConfig::LIBURING)

add_executable(stage4_uring_write_demo
  src/util/fd_guard.cpp
  examples/io_uring/uring_write_demo.cpp
)
target_compile_features(stage4_uring_write_demo PRIVATE cxx_std_20)
target_include_directories(stage4_uring_write_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(stage4_uring_write_demo PRIVATE PkgConfig::LIBURING)

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

add_test(
  NAME stage3_bench_sync
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage3_bench_sync>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage3_bench_sync_test.cmake
)

add_test(
  NAME stage3_bench_mmap
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage3_bench_mmap>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage3_bench_mmap_test.cmake
)

add_test(
  NAME stage3_benchmark_stats
  COMMAND stage3_benchmark_stats_test
)

add_test(
  NAME stage3_bench_pread
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage3_bench_pread>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage3_bench_pread_test.cmake
)

add_test(
  NAME stage4_uring_read_once
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage4_uring_read_once>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage4_uring_read_once_test.cmake
)

add_test(
  NAME stage4_uring_batch_read
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage4_uring_batch_read>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage4_uring_batch_read_test.cmake
)

add_test(
  NAME stage4_uring_write_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage4_uring_write_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage4_uring_write_demo_test.cmake
)
