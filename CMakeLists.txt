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
find_package(Python3 REQUIRED COMPONENTS Interpreter)
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

add_executable(stage5_task_test
  tests/stage5_task_test.cpp
)
target_compile_features(stage5_task_test PRIVATE cxx_std_20)
target_include_directories(stage5_task_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(stage5_completion_request_test
  tests/stage5_completion_request_test.cpp
)
target_compile_features(stage5_completion_request_test PRIVATE cxx_std_20)
target_include_directories(
  stage5_completion_request_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(stage5_async_read_demo
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/coroutine/uring_read_awaiter.cpp
  examples/io_uring/async_read_demo.cpp
)
target_compile_features(stage5_async_read_demo PRIVATE cxx_std_20)
target_include_directories(
  stage5_async_read_demo
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(stage5_async_read_demo PRIVATE PkgConfig::LIBURING)

add_executable(stage6_io_backend_contract_test
  tests/stage6_io_backend_contract_test.cpp
)
target_compile_features(stage6_io_backend_contract_test PRIVATE cxx_std_20)
target_include_directories(
  stage6_io_backend_contract_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(stage6_sync_backend_test
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/backend/sync_backend.cpp
  tests/stage6_sync_backend_test.cpp
)
target_compile_features(stage6_sync_backend_test PRIVATE cxx_std_20)
target_include_directories(
  stage6_sync_backend_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(stage6_uring_backend_test
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/coroutine/uring_read_awaiter.cpp
  src/backend/uring_backend.cpp
  tests/stage6_uring_backend_test.cpp
)
target_compile_features(stage6_uring_backend_test PRIVATE cxx_std_20)
target_include_directories(
  stage6_uring_backend_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(stage6_uring_backend_test PRIVATE PkgConfig::LIBURING)

add_executable(stage6_thread_pool_backend_test
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/backend/thread_pool_backend.cpp
  tests/stage6_thread_pool_backend_test.cpp
)
target_compile_features(stage6_thread_pool_backend_test PRIVATE cxx_std_20)
target_include_directories(
  stage6_thread_pool_backend_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(stage6_thread_pool_backend_test PRIVATE Threads::Threads)

add_executable(stage6_backend_factory_test
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/coroutine/uring_read_awaiter.cpp
  src/backend/sync_backend.cpp
  src/backend/uring_backend.cpp
  src/backend/thread_pool_backend.cpp
  src/backend/backend_factory.cpp
  tests/stage6_backend_factory_test.cpp
)
target_compile_features(stage6_backend_factory_test PRIVATE cxx_std_20)
target_include_directories(
  stage6_backend_factory_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(
  stage6_backend_factory_test
  PRIVATE PkgConfig::LIBURING Threads::Threads
)

add_executable(stage6_backend_fallback_demo
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/coroutine/uring_read_awaiter.cpp
  src/backend/sync_backend.cpp
  src/backend/uring_backend.cpp
  src/backend/thread_pool_backend.cpp
  src/backend/backend_factory.cpp
  examples/backend_fallback_demo.cpp
)
target_compile_features(stage6_backend_fallback_demo PRIVATE cxx_std_20)
target_include_directories(
  stage6_backend_fallback_demo
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(
  stage6_backend_fallback_demo
  PRIVATE PkgConfig::LIBURING Threads::Threads
)

add_library(asyncdataloader_pipeline
  src/pipeline/block_work_item.cpp
  src/pipeline/builtin_stages.cpp
  src/pipeline/pipeline.cpp
  src/pipeline/pipeline_executor.cpp
)
target_compile_features(asyncdataloader_pipeline PUBLIC cxx_std_20)
target_include_directories(
  asyncdataloader_pipeline
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(stage7_pipeline_registration_test
  tests/stage7_pipeline_registration_test.cpp
)
target_compile_features(stage7_pipeline_registration_test PRIVATE cxx_std_20)
target_link_libraries(
  stage7_pipeline_registration_test
  PRIVATE asyncdataloader_pipeline
)

add_executable(stage7_builtin_stages_test
  tests/stage7_builtin_stages_test.cpp
)
target_compile_features(stage7_builtin_stages_test PRIVATE cxx_std_20)
target_link_libraries(
  stage7_builtin_stages_test
  PRIVATE asyncdataloader_pipeline
)

add_executable(stage7_custom_stage_demo
  examples/stage7_custom_stage_demo.cpp
)
target_compile_features(stage7_custom_stage_demo PRIVATE cxx_std_20)
target_link_libraries(
  stage7_custom_stage_demo
  PRIVATE asyncdataloader_pipeline
)

add_library(asyncdataloader_config
  src/config/pipeline_config.cpp
)
target_compile_features(asyncdataloader_config PUBLIC cxx_std_20)
target_include_directories(
  asyncdataloader_config
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(stage8_pipeline_config_test
  tests/stage8_pipeline_config_test.cpp
)
target_compile_features(stage8_pipeline_config_test PRIVATE cxx_std_20)
target_link_libraries(
  stage8_pipeline_config_test
  PRIVATE asyncdataloader_config
)

add_library(asyncdataloader_buffer
  src/buffer/aligned_buffer.cpp
  src/buffer/aligned_buffer_pool.cpp
  src/buffer/buffer_handle.cpp
)
target_compile_features(asyncdataloader_buffer PUBLIC cxx_std_20)
target_include_directories(
  asyncdataloader_buffer
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(
  asyncdataloader_buffer
  PUBLIC asyncdataloader_config Threads::Threads
)
target_link_libraries(
  asyncdataloader_pipeline
  PUBLIC asyncdataloader_buffer
)

add_executable(stage8_aligned_buffer_test
  tests/stage8_aligned_buffer_test.cpp
)
target_compile_features(stage8_aligned_buffer_test PRIVATE cxx_std_20)
target_link_libraries(
  stage8_aligned_buffer_test
  PRIVATE asyncdataloader_buffer
)

add_executable(stage8_buffer_handle_test
  tests/stage8_buffer_handle_test.cpp
)
target_compile_features(stage8_buffer_handle_test PRIVATE cxx_std_20)
target_link_libraries(
  stage8_buffer_handle_test
  PRIVATE asyncdataloader_buffer
)

add_executable(stage8_buffer_pool_backpressure_test
  tests/stage8_buffer_pool_backpressure_test.cpp
)
target_compile_features(
  stage8_buffer_pool_backpressure_test
  PRIVATE cxx_std_20
)
target_link_libraries(
  stage8_buffer_pool_backpressure_test
  PRIVATE asyncdataloader_buffer
)

add_executable(stage8_spsc_queue_test
  tests/stage8_spsc_queue_test.cpp
)
target_compile_features(stage8_spsc_queue_test PRIVATE cxx_std_20)
target_link_libraries(
  stage8_spsc_queue_test
  PRIVATE asyncdataloader_buffer
)

add_executable(stage8_backpressure_demo
  examples/stage8_backpressure_demo.cpp
)
target_compile_features(stage8_backpressure_demo PRIVATE cxx_std_20)
target_link_libraries(
  stage8_backpressure_demo
  PRIVATE asyncdataloader_buffer
)

add_library(asyncdataloader_metrics
  src/metrics/counter.cpp
  src/metrics/gauge.cpp
  src/metrics/histogram.cpp
  src/metrics/metrics_registry.cpp
  src/metrics/scoped_timer.cpp
)
target_compile_features(asyncdataloader_metrics PUBLIC cxx_std_20)
target_include_directories(
  asyncdataloader_metrics
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(
  asyncdataloader_pipeline
  PUBLIC asyncdataloader_metrics
)
target_link_libraries(
  asyncdataloader_buffer
  PUBLIC asyncdataloader_metrics
)

add_executable(stage9_counter_test
  tests/stage9_counter_test.cpp
)
target_compile_features(stage9_counter_test PRIVATE cxx_std_20)
target_link_libraries(
  stage9_counter_test
  PRIVATE asyncdataloader_metrics Threads::Threads
)

add_executable(stage9_gauge_test
  tests/stage9_gauge_test.cpp
)
target_compile_features(stage9_gauge_test PRIVATE cxx_std_20)
target_link_libraries(
  stage9_gauge_test
  PRIVATE asyncdataloader_metrics Threads::Threads
)

add_executable(stage9_histogram_test
  tests/stage9_histogram_test.cpp
)
target_compile_features(stage9_histogram_test PRIVATE cxx_std_20)
target_link_libraries(
  stage9_histogram_test
  PRIVATE asyncdataloader_metrics Threads::Threads
)

add_executable(stage9_metrics_registry_test
  tests/stage9_metrics_registry_test.cpp
)
target_compile_features(stage9_metrics_registry_test PRIVATE cxx_std_20)
target_link_libraries(
  stage9_metrics_registry_test
  PRIVATE asyncdataloader_metrics
)

add_executable(stage9_scoped_timer_test
  tests/stage9_scoped_timer_test.cpp
)
target_compile_features(stage9_scoped_timer_test PRIVATE cxx_std_20)
target_link_libraries(
  stage9_scoped_timer_test
  PRIVATE asyncdataloader_metrics Threads::Threads
)

add_executable(stage9_stage_timing_test
  tests/stage9_stage_timing_test.cpp
)
target_compile_features(stage9_stage_timing_test PRIVATE cxx_std_20)
target_link_libraries(
  stage9_stage_timing_test
  PRIVATE asyncdataloader_pipeline
)

add_executable(stage9_metrics_snapshot_test
  tests/stage9_metrics_snapshot_test.cpp
)
target_compile_features(stage9_metrics_snapshot_test PRIVATE cxx_std_20)
target_link_libraries(
  stage9_metrics_snapshot_test
  PRIVATE asyncdataloader_buffer
)

add_library(asyncdataloader_reporting
  src/reporting/pipeline_reporter.cpp
)
target_compile_features(asyncdataloader_reporting PUBLIC cxx_std_20)
target_include_directories(
  asyncdataloader_reporting
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(
  asyncdataloader_reporting
  PUBLIC asyncdataloader_pipeline
)

add_executable(stage12_pipeline_reporter_test
  tests/stage12_pipeline_reporter_test.cpp
)
target_compile_features(stage12_pipeline_reporter_test PRIVATE cxx_std_20)
target_link_libraries(
  stage12_pipeline_reporter_test
  PRIVATE asyncdataloader_reporting
)

add_executable(stage10_block_work_item_test
  tests/stage10_block_work_item_test.cpp
)
target_compile_features(stage10_block_work_item_test PRIVATE cxx_std_20)
target_link_libraries(
  stage10_block_work_item_test
  PRIVATE asyncdataloader_pipeline
)

add_executable(stage10_queue_lifecycle_test
  tests/stage10_queue_lifecycle_test.cpp
)
target_compile_features(stage10_queue_lifecycle_test PRIVATE cxx_std_20)
target_link_libraries(
  stage10_queue_lifecycle_test
  PRIVATE asyncdataloader_pipeline
)

add_executable(stage10_pipeline_executor_test
  src/backend/sync_backend.cpp
  src/backend/thread_pool_backend.cpp
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  tests/stage10_pipeline_executor_test.cpp
)
target_compile_features(stage10_pipeline_executor_test PRIVATE cxx_std_20)
target_link_libraries(
  stage10_pipeline_executor_test
  PRIVATE asyncdataloader_pipeline
)

add_executable(stage10_reliable_output_test
  src/backend/sync_backend.cpp
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  tests/stage10_reliable_output_test.cpp
)
target_compile_features(stage10_reliable_output_test PRIVATE cxx_std_20)
target_link_libraries(
  stage10_reliable_output_test
  PRIVATE asyncdataloader_pipeline
)

add_executable(stage10_pipeline_metrics_test
  src/backend/sync_backend.cpp
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  tests/stage10_pipeline_metrics_test.cpp
)
target_compile_features(stage10_pipeline_metrics_test PRIVATE cxx_std_20)
target_link_libraries(
  stage10_pipeline_metrics_test
  PRIVATE asyncdataloader_pipeline
)

add_executable(preprocess_pipeline_demo
  src/backend/backend_factory.cpp
  src/backend/sync_backend.cpp
  src/backend/thread_pool_backend.cpp
  src/backend/uring_backend.cpp
  src/coroutine/uring_read_awaiter.cpp
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  examples/preprocess_pipeline_demo.cpp
)
target_compile_features(preprocess_pipeline_demo PRIVATE cxx_std_20)
target_link_libraries(
  preprocess_pipeline_demo
  PRIVATE
    asyncdataloader_pipeline
    asyncdataloader_reporting
    PkgConfig::LIBURING
    Threads::Threads
)

add_library(asyncdataloader_stage11_benchmark
  src/benchmark/end_to_end_baseline.cpp
)
target_compile_features(asyncdataloader_stage11_benchmark PUBLIC cxx_std_20)
target_include_directories(
  asyncdataloader_stage11_benchmark
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(
  asyncdataloader_stage11_benchmark
  PUBLIC asyncdataloader_pipeline
)

add_executable(stage11_end_to_end_baseline_test
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/util/timer.cpp
  tests/stage11_end_to_end_baseline_test.cpp
)
target_compile_features(stage11_end_to_end_baseline_test PRIVATE cxx_std_20)
target_link_libraries(
  stage11_end_to_end_baseline_test
  PRIVATE asyncdataloader_stage11_benchmark
)

add_executable(stage11_bench_end_to_end
  src/backend/sync_backend.cpp
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/util/timer.cpp
  benchmark/stage11_bench_end_to_end.cpp
)
target_compile_features(stage11_bench_end_to_end PRIVATE cxx_std_20)
target_link_libraries(
  stage11_bench_end_to_end
  PRIVATE
    asyncdataloader_stage11_benchmark
    asyncdataloader_benchmark
)

add_executable(stage11_bench_backends
  src/backend/backend_factory.cpp
  src/backend/sync_backend.cpp
  src/backend/thread_pool_backend.cpp
  src/backend/uring_backend.cpp
  src/coroutine/uring_read_awaiter.cpp
  src/util/fd_guard.cpp
  src/util/file_io.cpp
  src/util/timer.cpp
  benchmark/stage11_bench_backends.cpp
)
target_compile_features(stage11_bench_backends PRIVATE cxx_std_20)
target_link_libraries(
  stage11_bench_backends
  PRIVATE
    asyncdataloader_stage11_benchmark
    asyncdataloader_benchmark
    PkgConfig::LIBURING
    Threads::Threads
)

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

add_test(NAME stage5_task COMMAND stage5_task_test)
add_test(
  NAME stage5_completion_request
  COMMAND stage5_completion_request_test
)

add_test(
  NAME stage5_async_read_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage5_async_read_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage5_async_read_demo_test.cmake
)

add_test(
  NAME stage6_io_backend_contract
  COMMAND stage6_io_backend_contract_test
)

add_test(
  NAME stage6_sync_backend
  COMMAND stage6_sync_backend_test
)

add_test(
  NAME stage6_uring_backend
  COMMAND stage6_uring_backend_test
)

add_test(
  NAME stage6_thread_pool_backend
  COMMAND stage6_thread_pool_backend_test
)

add_test(
  NAME stage6_backend_factory
  COMMAND stage6_backend_factory_test
)

add_test(
  NAME stage6_backend_fallback_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage6_backend_fallback_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage6_backend_fallback_demo_test.cmake
)

add_test(
  NAME stage7_pipeline_registration
  COMMAND stage7_pipeline_registration_test
)

add_test(
  NAME stage7_builtin_stages
  COMMAND stage7_builtin_stages_test
)

add_test(
  NAME stage7_custom_stage_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage7_custom_stage_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage7_custom_stage_demo_test.cmake
)

add_test(
  NAME stage8_pipeline_config
  COMMAND stage8_pipeline_config_test
)

add_test(
  NAME stage8_aligned_buffer
  COMMAND stage8_aligned_buffer_test
)

add_test(
  NAME stage8_buffer_handle
  COMMAND stage8_buffer_handle_test
)

add_test(
  NAME stage8_buffer_pool_backpressure
  COMMAND stage8_buffer_pool_backpressure_test
)
set_tests_properties(
  stage8_buffer_pool_backpressure
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage8_spsc_queue
  COMMAND stage8_spsc_queue_test
)
set_tests_properties(
  stage8_spsc_queue
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage8_backpressure_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage8_backpressure_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage8_backpressure_demo_test.cmake
)
set_tests_properties(
  stage8_backpressure_demo
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage9_counter
  COMMAND stage9_counter_test
)
set_tests_properties(
  stage9_counter
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage9_gauge
  COMMAND stage9_gauge_test
)
set_tests_properties(
  stage9_gauge
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage9_histogram
  COMMAND stage9_histogram_test
)
set_tests_properties(
  stage9_histogram
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage9_metrics_registry
  COMMAND stage9_metrics_registry_test
)
set_tests_properties(
  stage9_metrics_registry
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage9_scoped_timer
  COMMAND stage9_scoped_timer_test
)
set_tests_properties(
  stage9_scoped_timer
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage9_stage_timing
  COMMAND stage9_stage_timing_test
)
set_tests_properties(
  stage9_stage_timing
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage9_metrics_snapshot
  COMMAND stage9_metrics_snapshot_test
)
set_tests_properties(
  stage9_metrics_snapshot
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage10_block_work_item
  COMMAND stage10_block_work_item_test
)
set_tests_properties(
  stage10_block_work_item
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage10_queue_lifecycle
  COMMAND stage10_queue_lifecycle_test
)
set_tests_properties(
  stage10_queue_lifecycle
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage10_pipeline_executor
  COMMAND stage10_pipeline_executor_test
)
set_tests_properties(
  stage10_pipeline_executor
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage10_reliable_output
  COMMAND stage10_reliable_output_test
)
set_tests_properties(
  stage10_reliable_output
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage10_pipeline_metrics
  COMMAND stage10_pipeline_metrics_test
)
set_tests_properties(
  stage10_pipeline_metrics
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage10_preprocess_pipeline_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:preprocess_pipeline_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage10_preprocess_pipeline_demo_test.cmake
)
set_tests_properties(
  stage10_preprocess_pipeline_demo
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage11_end_to_end_baseline
  COMMAND stage11_end_to_end_baseline_test
)
set_tests_properties(
  stage11_end_to_end_baseline
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage11_bench_end_to_end
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage11_bench_end_to_end>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage11_bench_end_to_end_test.cmake
)
set_tests_properties(
  stage11_bench_end_to_end
  PROPERTIES TIMEOUT 20
)

add_test(
  NAME stage11_bench_backends
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:stage11_bench_backends>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage11_bench_backends_test.cmake
)
set_tests_properties(
  stage11_bench_backends
  PROPERTIES TIMEOUT 30
)

add_test(
  NAME stage11_parameter_sweep
  COMMAND
    ${CMAKE_COMMAND}
    -D PYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -D SCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/benchmark/stage11_parameter_sweep.py
    -D PIPELINE_EXECUTABLE=$<TARGET_FILE:preprocess_pipeline_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage11_parameter_sweep_test.cmake
)
set_tests_properties(
  stage11_parameter_sweep
  PROPERTIES TIMEOUT 30
)

add_test(
  NAME stage11_analyze_results
  COMMAND
    ${CMAKE_COMMAND}
    -D PYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -D SCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/benchmark/stage11_analyze_results.py
    -D FIXTURE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/stage11_analysis_fixture.csv
    -D SWEEP_SCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/benchmark/stage11_parameter_sweep.py
    -D PIPELINE_EXECUTABLE=$<TARGET_FILE:preprocess_pipeline_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage11_analyze_results_test.cmake
)
set_tests_properties(
  stage11_analyze_results
  PROPERTIES TIMEOUT 20
)

add_test(
  NAME stage11_capture_profile
  COMMAND
    ${CMAKE_COMMAND}
    -D PYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -D SCRIPT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/benchmark/stage11_capture_profile.py
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage11_capture_profile_test.cmake
)
set_tests_properties(
  stage11_capture_profile
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage12_pipeline_reporter
  COMMAND stage12_pipeline_reporter_test
)
set_tests_properties(
  stage12_pipeline_reporter
  PROPERTIES TIMEOUT 10
)

add_test(
  NAME stage12_preprocess_pipeline_demo
  COMMAND
    ${CMAKE_COMMAND}
    -D EXECUTABLE_PATH=$<TARGET_FILE:preprocess_pipeline_demo>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage12_preprocess_pipeline_demo_test.cmake
)
set_tests_properties(
  stage12_preprocess_pipeline_demo
  PROPERTIES TIMEOUT 10
)
