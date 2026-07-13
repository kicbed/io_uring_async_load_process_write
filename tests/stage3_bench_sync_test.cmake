if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

get_filename_component(executable_dir "${EXECUTABLE_PATH}" DIRECTORY)

set(input_path "${executable_dir}/stage3_bench_sync_input.bin")
set(output_path "${executable_dir}/stage3_bench_sync_output.bin")

file(WRITE "${input_path}" "abcXYZ")
file(REMOVE "${output_path}")

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}" "${output_path}" "2" "3"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "stage3_bench_sync failed\n"
    "result=${result}\n"
    "stdout=${stdout}\n"
    "stderr=${stderr}"
  )
endif()

file(READ "${output_path}" output_contents)
if(NOT output_contents STREQUAL "ABCxyz")
  message(FATAL_ERROR
    "stage3_bench_sync wrote unexpected output: '${output_contents}'"
  )
endif()

if(NOT stdout MATCHES "name,bytes_per_iteration,bytes_written_per_iteration,sample_count,total_elapsed_ms,average_ms,p50_ms,p95_ms,p99_ms,throughput_mib_s")
  message(FATAL_ERROR "benchmark stdout should include shared CSV header, got: ${stdout}")
endif()

if(NOT stdout MATCHES "sync_baseline,6,6,3,")
  message(FATAL_ERROR "benchmark stdout should include sync_baseline CSV row, got: ${stdout}")
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}" "${output_path}" "2"
  RESULT_VARIABLE default_result
  OUTPUT_VARIABLE default_stdout
  ERROR_VARIABLE default_stderr
)

if(NOT default_result EQUAL 0 OR NOT default_stdout MATCHES "sync_baseline,6,6,1,")
  message(FATAL_ERROR
    "omitting sync iterations should default to one run\n"
    "result=${default_result}\n"
    "stdout=${default_stdout}\n"
    "stderr=${default_stderr}"
  )
endif()

foreach(invalid_args IN ITEMS "0" "2junk" "2;0" "2;3junk")
  execute_process(
    COMMAND "${EXECUTABLE_PATH}" "${input_path}" "${output_path}" ${invalid_args}
    RESULT_VARIABLE invalid_result
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(invalid_result EQUAL 0)
    message(FATAL_ERROR "invalid sync benchmark arguments should fail: ${invalid_args}")
  endif()
endforeach()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}.missing" "${output_path}" "2"
  RESULT_VARIABLE missing_result
  OUTPUT_QUIET
  ERROR_QUIET
)

if(missing_result EQUAL 0)
  message(FATAL_ERROR "missing sync input should fail")
endif()

file(REMOVE "${input_path}" "${output_path}")
