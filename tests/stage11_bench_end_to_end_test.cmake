if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TEST_SUFFIX)
set(TEST_DIRECTORY "/tmp/asyncdataloader_stage11_bench_${TEST_SUFFIX}")
set(INPUT_PATH "${TEST_DIRECTORY}/input.bin")
set(SERIAL_OUTPUT_PATH "${TEST_DIRECTORY}/serial.bin")
set(NO_OVERLAP_OUTPUT_PATH "${TEST_DIRECTORY}/no-overlap.bin")
set(OVERLAP_OUTPUT_PATH "${TEST_DIRECTORY}/overlap.bin")

file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
file(WRITE "${INPUT_PATH}" "abcXYZ~")

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${SERIAL_OUTPUT_PATH}"
    "${NO_OVERLAP_OUTPUT_PATH}"
    "${OVERLAP_OUTPUT_PATH}"
    4096
    3
    1
    2
  RESULT_VARIABLE RESULT
  OUTPUT_VARIABLE STDOUT
  ERROR_VARIABLE STDERR
)

if(NOT RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "Stage 11.1 benchmark failed (${RESULT})\nstdout:\n${STDOUT}\nstderr:\n${STDERR}"
  )
endif()

foreach(EXPECTED_TEXT
    "name,bytes_per_iteration,bytes_written_per_iteration,sample_count"
    "serial_sync_byte_increment,7,7,2,"
    "pipeline_sync_no_overlap_byte_increment,7,7,2,"
    "pipeline_sync_overlap_byte_increment,7,7,2,")
  string(FIND "${STDOUT}" "${EXPECTED_TEXT}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR
      "benchmark stdout missed '${EXPECTED_TEXT}'\n${STDOUT}"
    )
  endif()
endforeach()

foreach(EXPECTED_TEXT
    "comparison_design=serial_oracle_plus_overlap_ablation"
    "processing_stage=byte_increment"
    "serial_execution=one_thread_blocking_pread_process_pwrite"
    "pipeline_execution=reader_processor_writer_jthreads"
    "read_backend=sync"
    "coroutine_behavior=sync_task_completes_in_reader_thread"
    "no_overlap_max_inflight_buffers=1"
    "overlap_max_inflight_buffers=3"
    "timing_boundary=execution_plus_output_fsync"
    "execution_order=rotating_three_methods"
    "pipeline_metrics=enabled"
    "output_verified=true"
    "verified_bytes=7")
  string(FIND "${STDERR}" "${EXPECTED_TEXT}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR
      "benchmark stderr missed '${EXPECTED_TEXT}'\n${STDERR}"
    )
  endif()
endforeach()

file(READ "${SERIAL_OUTPUT_PATH}" SERIAL_HEX HEX)
file(READ "${NO_OVERLAP_OUTPUT_PATH}" NO_OVERLAP_HEX HEX)
file(READ "${OVERLAP_OUTPUT_PATH}" OVERLAP_HEX HEX)
if(NOT SERIAL_HEX STREQUAL "626364595a5b7f" OR
   NOT NO_OVERLAP_HEX STREQUAL SERIAL_HEX OR
   NOT OVERLAP_HEX STREQUAL SERIAL_HEX)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "Stage 11.1 outputs were not the expected +1 bytes")
endif()

set(MULTI_BLOCK_INPUT_PATH "${TEST_DIRECTORY}/multi-block-input.bin")
set(MULTI_BLOCK_SERIAL_PATH "${TEST_DIRECTORY}/multi-block-serial.bin")
set(MULTI_BLOCK_NO_OVERLAP_PATH
  "${TEST_DIRECTORY}/multi-block-no-overlap.bin"
)
set(MULTI_BLOCK_OVERLAP_PATH "${TEST_DIRECTORY}/multi-block-overlap.bin")
string(REPEAT "a" 12289 MULTI_BLOCK_INPUT)
file(WRITE "${MULTI_BLOCK_INPUT_PATH}" "${MULTI_BLOCK_INPUT}")
execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${MULTI_BLOCK_INPUT_PATH}"
    "${MULTI_BLOCK_SERIAL_PATH}"
    "${MULTI_BLOCK_NO_OVERLAP_PATH}"
    "${MULTI_BLOCK_OVERLAP_PATH}"
    4096
    3
    1
    3
  RESULT_VARIABLE MULTI_BLOCK_RESULT
  OUTPUT_VARIABLE MULTI_BLOCK_STDOUT
  ERROR_VARIABLE MULTI_BLOCK_STDERR
)
if(NOT MULTI_BLOCK_RESULT EQUAL 0 OR
   NOT MULTI_BLOCK_STDOUT MATCHES
       "serial_sync_byte_increment,12289,12289,3," OR
   NOT MULTI_BLOCK_STDOUT MATCHES
       "pipeline_sync_no_overlap_byte_increment,12289,12289,3," OR
   NOT MULTI_BLOCK_STDOUT MATCHES
       "pipeline_sync_overlap_byte_increment,12289,12289,3," OR
   NOT MULTI_BLOCK_STDERR MATCHES "verified_bytes=12289")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "multi-block Stage 11.1 flow failed\n"
    "stdout:\n${MULTI_BLOCK_STDOUT}\nstderr:\n${MULTI_BLOCK_STDERR}"
  )
endif()

foreach(MULTI_BLOCK_OUTPUT
    "${MULTI_BLOCK_SERIAL_PATH}"
    "${MULTI_BLOCK_NO_OVERLAP_PATH}"
    "${MULTI_BLOCK_OVERLAP_PATH}")
  file(SIZE "${MULTI_BLOCK_OUTPUT}" MULTI_BLOCK_OUTPUT_SIZE)
  if(NOT MULTI_BLOCK_OUTPUT_SIZE EQUAL 12289)
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR "a Stage 11.1 multi-block output had wrong size")
  endif()
endforeach()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${INPUT_PATH}"
    "${NO_OVERLAP_OUTPUT_PATH}"
    "${OVERLAP_OUTPUT_PATH}"
    4096
    3
    1
  RESULT_VARIABLE SAME_INPUT_OUTPUT_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)
file(READ "${INPUT_PATH}" INPUT_AFTER_REJECTION)
if(SAME_INPUT_OUTPUT_RESULT EQUAL 0 OR
   NOT INPUT_AFTER_REJECTION STREQUAL "abcXYZ~")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "input/output alias must be rejected before truncating the input"
  )
endif()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${SERIAL_OUTPUT_PATH}"
    "${NO_OVERLAP_OUTPUT_PATH}"
    "${OVERLAP_OUTPUT_PATH}"
    7
    3
    1
  RESULT_VARIABLE INVALID_ALIGNMENT_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)
if(INVALID_ALIGNMENT_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "misaligned block_size should fail")
endif()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${SERIAL_OUTPUT_PATH}"
    "${NO_OVERLAP_OUTPUT_PATH}"
    "${OVERLAP_OUTPUT_PATH}"
    4096
    2
    1
  RESULT_VARIABLE TOO_FEW_OVERLAP_BUFFERS_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)
if(TOO_FEW_OVERLAP_BUFFERS_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "three-stage overlap control should require at least three buffers"
  )
endif()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}.missing"
    "${SERIAL_OUTPUT_PATH}"
    "${NO_OVERLAP_OUTPUT_PATH}"
    "${OVERLAP_OUTPUT_PATH}"
    4096
    3
    1
  RESULT_VARIABLE MISSING_INPUT_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)
if(MISSING_INPUT_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "missing input should fail")
endif()

file(WRITE "${INPUT_PATH}" "")
execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${SERIAL_OUTPUT_PATH}"
    "${NO_OVERLAP_OUTPUT_PATH}"
    "${OVERLAP_OUTPUT_PATH}"
    4096
    3
    1
  RESULT_VARIABLE EMPTY_RESULT
  OUTPUT_VARIABLE EMPTY_STDOUT
  ERROR_VARIABLE EMPTY_STDERR
)
if(NOT EMPTY_RESULT EQUAL 0 OR
   NOT EMPTY_STDOUT MATCHES "serial_sync_byte_increment,0,0,1," OR
   NOT EMPTY_STDOUT MATCHES "pipeline_sync_no_overlap_byte_increment,0,0,1," OR
   NOT EMPTY_STDOUT MATCHES "pipeline_sync_overlap_byte_increment,0,0,1," OR
   NOT EMPTY_STDERR MATCHES "verified_bytes=0")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "empty input contract failed\nstdout:\n${EMPTY_STDOUT}\nstderr:\n${EMPTY_STDERR}"
  )
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
