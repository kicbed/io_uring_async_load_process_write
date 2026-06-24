if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

set(input_path "${CMAKE_CURRENT_BINARY_DIR}/stage2_demo_input.bin")
set(output_path "${CMAKE_CURRENT_BINARY_DIR}/stage2_demo_output.bin")

file(WRITE "${input_path}" "abcXYZ")
file(REMOVE "${output_path}")

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}" "${output_path}" "2"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "stage2_sync_preprocess_demo failed\n"
    "result=${result}\n"
    "stdout=${stdout}\n"
    "stderr=${stderr}"
  )
endif()

file(READ "${output_path}" output_contents)
if(NOT output_contents STREQUAL "ABCxyz")
  message(FATAL_ERROR
    "stage2_sync_preprocess_demo wrote unexpected output: '${output_contents}'"
  )
endif()

if(NOT stdout MATCHES "bytes_read=6")
  message(FATAL_ERROR "demo stdout should report bytes_read=6, got: ${stdout}")
endif()

if(NOT stdout MATCHES "bytes_written=6")
  message(FATAL_ERROR "demo stdout should report bytes_written=6, got: ${stdout}")
endif()

file(REMOVE "${input_path}" "${output_path}")
