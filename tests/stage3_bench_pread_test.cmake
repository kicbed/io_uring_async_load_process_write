if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

get_filename_component(executable_dir "${EXECUTABLE_PATH}" DIRECTORY)
set(input_path "${executable_dir}/stage3_bench_pread_input.bin")
set(empty_input_path "${executable_dir}/stage3_bench_pread_empty.bin")

file(WRITE "${input_path}" "abcXYZ")
file(WRITE "${empty_input_path}" "")

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}" "2" "3"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "stage3_bench_pread failed\n"
    "result=${result}\n"
    "stdout=${stdout}\n"
    "stderr=${stderr}"
  )
endif()

if(NOT stdout MATCHES "name,bytes_per_iteration,bytes_written_per_iteration,sample_count,total_elapsed_ms,average_ms,p50_ms,p95_ms,p99_ms,throughput_mib_s")
  message(FATAL_ERROR "benchmark stdout should include shared CSV header, got: ${stdout}")
endif()

if(NOT stdout MATCHES "pread_scan,6,0,3,")
  message(FATAL_ERROR "benchmark stdout should include pread_scan CSV row, got: ${stdout}")
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${empty_input_path}" "2" "2"
  RESULT_VARIABLE empty_result
  OUTPUT_VARIABLE empty_stdout
  ERROR_VARIABLE empty_stderr
)

if(NOT empty_result EQUAL 0 OR NOT empty_stdout MATCHES "pread_scan,0,0,2,")
  message(FATAL_ERROR
    "empty pread input should succeed\n"
    "result=${empty_result}\n"
    "stdout=${empty_stdout}\n"
    "stderr=${empty_stderr}"
  )
endif()

foreach(invalid_args IN ITEMS "0;1" "2;0" "2;3junk")
  execute_process(
    COMMAND "${EXECUTABLE_PATH}" "${input_path}" ${invalid_args}
    RESULT_VARIABLE invalid_result
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(invalid_result EQUAL 0)
    message(FATAL_ERROR "invalid pread benchmark arguments should fail: ${invalid_args}")
  endif()
endforeach()

file(REMOVE "${input_path}" "${empty_input_path}")
