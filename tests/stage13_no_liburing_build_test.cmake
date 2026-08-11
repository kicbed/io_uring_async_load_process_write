if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TEST_SUFFIX)
set(TEST_ROOT "/tmp/asyncdataloader_stage13_no_liburing_${TEST_SUFFIX}")
set(BUILD_DIR "${TEST_ROOT}/build")
set(INPUT_PATH "${TEST_ROOT}/input.bin")
set(OUTPUT_PATH "${TEST_ROOT}/output.bin")
set(EXPLICIT_OUTPUT_PATH "${TEST_ROOT}/explicit-uring.bin")

function(fail_with_cleanup)
  file(REMOVE_RECURSE "${TEST_ROOT}")
  message(FATAL_ERROR "${ARGV}")
endfunction()

file(MAKE_DIRECTORY "${TEST_ROOT}")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    -S "${SOURCE_DIR}"
    -B "${BUILD_DIR}"
    -D CMAKE_BUILD_TYPE=Debug
    -D ASYNCDATALOADER_ENABLE_LIBURING=OFF
  RESULT_VARIABLE CONFIGURE_RESULT
  OUTPUT_VARIABLE CONFIGURE_STDOUT
  ERROR_VARIABLE CONFIGURE_STDERR
)
if(NOT CONFIGURE_RESULT EQUAL 0)
  fail_with_cleanup(
    "no-liburing configure failed (${CONFIGURE_RESULT})\n"
    "stdout:\n${CONFIGURE_STDOUT}\n"
    "stderr:\n${CONFIGURE_STDERR}"
  )
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    --build "${BUILD_DIR}"
    --target
      stage6_backend_factory_test
      preprocess_pipeline_demo
    --parallel 2
  RESULT_VARIABLE BUILD_RESULT
  OUTPUT_VARIABLE BUILD_STDOUT
  ERROR_VARIABLE BUILD_STDERR
)
if(NOT BUILD_RESULT EQUAL 0)
  fail_with_cleanup(
    "no-liburing fallback targets failed to build (${BUILD_RESULT})\n"
    "stdout:\n${BUILD_STDOUT}\n"
    "stderr:\n${BUILD_STDERR}"
  )
endif()

execute_process(
  COMMAND "${BUILD_DIR}/stage6_backend_factory_test"
  RESULT_VARIABLE FACTORY_RESULT
  OUTPUT_VARIABLE FACTORY_STDOUT
  ERROR_VARIABLE FACTORY_STDERR
)
if(NOT FACTORY_RESULT EQUAL 0)
  fail_with_cleanup(
    "no-liburing BackendFactory test failed (${FACTORY_RESULT})\n"
    "stdout:\n${FACTORY_STDOUT}\n"
    "stderr:\n${FACTORY_STDERR}"
  )
endif()

file(WRITE "${INPUT_PATH}" "abc")
execute_process(
  COMMAND
    "${BUILD_DIR}/preprocess_pipeline_demo"
    "${INPUT_PATH}"
    "${OUTPUT_PATH}"
    --backend=auto
    --block-size=4096
    --buffers=3
    --queue-depth=1
    --thread-workers=1
    --report-ms=0
  RESULT_VARIABLE AUTO_RESULT
  OUTPUT_VARIABLE AUTO_STDOUT
  ERROR_VARIABLE AUTO_STDERR
)
if(NOT AUTO_RESULT EQUAL 0 OR
   NOT AUTO_STDOUT MATCHES "selected_backend=thread_pool")
  fail_with_cleanup(
    "no-liburing Auto pipeline did not select ThreadPool\n"
    "result=${AUTO_RESULT}\n"
    "stdout:\n${AUTO_STDOUT}\n"
    "stderr:\n${AUTO_STDERR}"
  )
endif()

file(READ "${OUTPUT_PATH}" OUTPUT_HEX HEX)
if(NOT OUTPUT_HEX STREQUAL "626364")
  fail_with_cleanup(
    "no-liburing Auto pipeline output was not the expected bcd payload"
  )
endif()

execute_process(
  COMMAND
    "${BUILD_DIR}/preprocess_pipeline_demo"
    "${INPUT_PATH}"
    "${EXPLICIT_OUTPUT_PATH}"
    --backend=uring
    --report-ms=0
  RESULT_VARIABLE EXPLICIT_RESULT
  OUTPUT_VARIABLE EXPLICIT_STDOUT
  ERROR_VARIABLE EXPLICIT_STDERR
)
if(EXPLICIT_RESULT EQUAL 0 OR
   NOT EXPLICIT_STDERR MATCHES "not compiled")
  fail_with_cleanup(
    "explicit Uring did not fail clearly in a no-liburing build\n"
    "result=${EXPLICIT_RESULT}\n"
    "stdout:\n${EXPLICIT_STDOUT}\n"
    "stderr:\n${EXPLICIT_STDERR}"
  )
endif()
if(EXISTS "${EXPLICIT_OUTPUT_PATH}")
  fail_with_cleanup(
    "explicit unavailable Uring created an output before failing"
  )
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
