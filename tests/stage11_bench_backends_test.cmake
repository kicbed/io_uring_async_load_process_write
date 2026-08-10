if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TEST_SUFFIX)
set(TEST_DIRECTORY "/tmp/asyncdataloader_stage11_backends_${TEST_SUFFIX}")
set(INPUT_PATH "${TEST_DIRECTORY}/input.bin")
set(MULTI_BLOCK_INPUT_PATH "${TEST_DIRECTORY}/multi-block-input.bin")
set(OUTPUT_DIRECTORY "${TEST_DIRECTORY}/outputs")
set(FALLBACK_OUTPUT_DIRECTORY "${TEST_DIRECTORY}/fallback-outputs")

file(MAKE_DIRECTORY
  "${TEST_DIRECTORY}"
  "${OUTPUT_DIRECTORY}"
  "${FALLBACK_OUTPUT_DIRECTORY}"
)
file(WRITE "${INPUT_PATH}" "abcXYZ~")
string(REPEAT "a" 12289 MULTI_BLOCK_INPUT)
file(WRITE "${MULTI_BLOCK_INPUT_PATH}" "${MULTI_BLOCK_INPUT}")

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${OUTPUT_DIRECTORY}"
    4096
    3
    1
    2
    2
    --disable-uring
    --disable-threadpool
  RESULT_VARIABLE RESULT
  OUTPUT_VARIABLE STDOUT
  ERROR_VARIABLE STDERR
)

if(NOT RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "Stage 11.2 backend benchmark failed (${RESULT})\n"
    "stdout:\n${STDOUT}\nstderr:\n${STDERR}"
  )
endif()

foreach(EXPECTED_TEXT
    "name,bytes_per_iteration,bytes_written_per_iteration,sample_count"
    "pipeline_sync_byte_increment,7,7,2,"
    "pipeline_thread_pool_byte_increment,7,7,2,"
    "pipeline_auto_selected_sync_byte_increment,7,7,2,")
  string(FIND "${STDOUT}" "${EXPECTED_TEXT}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR
      "backend benchmark stdout missed '${EXPECTED_TEXT}'\n${STDOUT}"
    )
  endif()
endforeach()

foreach(EXPECTED_TEXT
    "comparison_design=same_pipeline_read_backend_matrix"
    "pipeline_execution=reader_processor_writer_jthreads"
    "sync_read_model=blocking_pread_in_reader_thread"
    "thread_pool_read_model=coroutine_suspend_worker_pread_resume"
    "io_uring_read_model=coroutine_suspend_sqe_cqe_resume"
    "read_inflight_model=one_read_task_at_a_time"
    "writer_io=blocking_pwrite_for_all_methods"
    "auto_is_selection_policy=true"
    "auto_selected_backend=sync"
    "backend_construction=outside_timing_fresh_per_sample"
    "execution_order=rotating_available_backends"
    "output_verified=true"
    "verified_bytes=7")
  string(FIND "${STDERR}" "${EXPECTED_TEXT}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR
      "backend benchmark stderr missed '${EXPECTED_TEXT}'\n${STDERR}"
    )
  endif()
endforeach()

foreach(OUTPUT_NAME serial-oracle.bin sync.bin thread-pool.bin auto.bin)
  file(READ "${OUTPUT_DIRECTORY}/${OUTPUT_NAME}" OUTPUT_HEX HEX)
  if(NOT OUTPUT_HEX STREQUAL "626364595a5b7f")
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR "${OUTPUT_NAME} did not contain the expected bytes")
  endif()
endforeach()

# io_uring is explicit and never mislabeled as a fallback. Some CI kernels
# may reject ring creation; in that case the benchmark reports it as skipped.
if(EXISTS "${OUTPUT_DIRECTORY}/io-uring.bin")
  file(READ "${OUTPUT_DIRECTORY}/io-uring.bin" URING_HEX HEX)
  if(NOT URING_HEX STREQUAL "626364595a5b7f" OR
     NOT STDOUT MATCHES "pipeline_io_uring_byte_increment,7,7,2,")
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR "available io_uring path failed correctness/reporting")
  endif()
else()
  if(NOT STDERR MATCHES "skipped_backend=io_uring")
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR "unavailable io_uring was not reported as skipped")
  endif()
endif()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${MULTI_BLOCK_INPUT_PATH}"
    "${FALLBACK_OUTPUT_DIRECTORY}"
    4096
    3
    1
    2
    --disable-uring
  RESULT_VARIABLE THREAD_POOL_FALLBACK_RESULT
  OUTPUT_VARIABLE THREAD_POOL_FALLBACK_STDOUT
  ERROR_VARIABLE THREAD_POOL_FALLBACK_STDERR
)
if(NOT THREAD_POOL_FALLBACK_RESULT EQUAL 0 OR
   NOT THREAD_POOL_FALLBACK_STDOUT MATCHES
       "pipeline_auto_selected_thread_pool_byte_increment,12289,12289,1," OR
   NOT THREAD_POOL_FALLBACK_STDERR MATCHES
       "auto_selected_backend=thread_pool")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "forced Auto -> ThreadPool selection failed\n"
    "stdout:\n${THREAD_POOL_FALLBACK_STDOUT}\n"
    "stderr:\n${THREAD_POOL_FALLBACK_STDERR}"
  )
endif()

file(SIZE
  "${FALLBACK_OUTPUT_DIRECTORY}/auto.bin"
  THREAD_POOL_FALLBACK_OUTPUT_SIZE
)
if(NOT THREAD_POOL_FALLBACK_OUTPUT_SIZE EQUAL 12289)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "multi-block Auto -> ThreadPool output had wrong size")
endif()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${TEST_DIRECTORY}/missing-output-directory"
    4096
    3
    1
    2
  RESULT_VARIABLE MISSING_DIRECTORY_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)
if(MISSING_DIRECTORY_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "a missing output directory should fail")
endif()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}.missing"
    "${OUTPUT_DIRECTORY}"
    4096
    3
    1
    2
  RESULT_VARIABLE MISSING_INPUT_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)
if(MISSING_INPUT_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "a missing input should fail")
endif()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${OUTPUT_DIRECTORY}"
    4096
    2
    1
    2
  RESULT_VARIABLE TOO_FEW_BUFFERS_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)
if(TOO_FEW_BUFFERS_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "backend matrix should require three overlap buffers")
endif()

set(ALIAS_DIRECTORY "${TEST_DIRECTORY}/alias-outputs")
set(ALIAS_INPUT "${ALIAS_DIRECTORY}/sync.bin")
file(MAKE_DIRECTORY "${ALIAS_DIRECTORY}")
file(WRITE "${ALIAS_INPUT}" "do-not-truncate")
execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${ALIAS_INPUT}"
    "${ALIAS_DIRECTORY}"
    4096
    3
    1
    2
  RESULT_VARIABLE ALIAS_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)
file(READ "${ALIAS_INPUT}" ALIAS_INPUT_AFTER_REJECTION)
if(ALIAS_RESULT EQUAL 0 OR
   NOT ALIAS_INPUT_AFTER_REJECTION STREQUAL "do-not-truncate")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "input/output alias must be rejected before truncate")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
