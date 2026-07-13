if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

get_filename_component(executable_dir "${EXECUTABLE_PATH}" DIRECTORY)

set(input_path "${executable_dir}/stage3_bench_mmap_input.bin")

file(WRITE "${input_path}" "abcXYZ")

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}" "3"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "stage3_bench_mmap failed\n"
    "result=${result}\n"
    "stdout=${stdout}\n"
    "stderr=${stderr}"
  )
endif()

if(NOT stdout MATCHES "name,bytes_per_iteration,bytes_written_per_iteration,sample_count,total_elapsed_ms,average_ms,p50_ms,p95_ms,p99_ms,throughput_mib_s")
  message(FATAL_ERROR "benchmark stdout should include CSV header, got: ${stdout}")
endif()

if(NOT stdout MATCHES "mmap_scan,6,0,3,")
  message(FATAL_ERROR "benchmark stdout should include mmap_scan CSV row, got: ${stdout}")
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}"
  RESULT_VARIABLE default_result
  OUTPUT_VARIABLE default_stdout
  ERROR_VARIABLE default_stderr
)

if(NOT default_result EQUAL 0 OR NOT default_stdout MATCHES "mmap_scan,6,0,1,")
  message(FATAL_ERROR
    "omitting iterations should default to one run\n"
    "result=${default_result}\n"
    "stdout=${default_stdout}\n"
    "stderr=${default_stderr}"
  )
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}" "0"
  RESULT_VARIABLE invalid_result
  OUTPUT_QUIET
  ERROR_QUIET
)

if(invalid_result EQUAL 0)
  message(FATAL_ERROR "zero iterations should be rejected")
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}" "3junk"
  RESULT_VARIABLE malformed_result
  OUTPUT_QUIET
  ERROR_QUIET
)

if(malformed_result EQUAL 0)
  message(FATAL_ERROR "malformed iterations should be rejected")
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}.missing"
  RESULT_VARIABLE missing_result
  OUTPUT_QUIET
  ERROR_QUIET
)

if(missing_result EQUAL 0)
  message(FATAL_ERROR "missing mmap input should fail")
endif()

set(empty_input_path "${executable_dir}/stage3_bench_mmap_empty.bin")
file(WRITE "${empty_input_path}" "")

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${empty_input_path}" "2"
  RESULT_VARIABLE empty_result
  OUTPUT_VARIABLE empty_stdout
  ERROR_VARIABLE empty_stderr
)

if(NOT empty_result EQUAL 0 OR NOT empty_stdout MATCHES "mmap_scan,0,0,2,0,0,0,0,0,0")
  message(FATAL_ERROR
    "empty input should report two zero-duration samples\n"
    "result=${empty_result}\n"
    "stdout=${empty_stdout}\n"
    "stderr=${empty_stderr}"
  )
endif()

file(REMOVE "${input_path}")
file(REMOVE "${empty_input_path}")
