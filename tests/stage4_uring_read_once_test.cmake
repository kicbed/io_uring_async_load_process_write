if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

get_filename_component(executable_dir "${EXECUTABLE_PATH}" DIRECTORY)
set(input_path "${executable_dir}/stage4_uring_read_once_input.bin")
set(empty_input_path "${executable_dir}/stage4_uring_read_once_empty.bin")
set(missing_input_path "${executable_dir}/stage4_uring_read_once_missing.bin")

file(WRITE "${input_path}" "abcXYZ")
file(WRITE "${empty_input_path}" "")
file(REMOVE "${missing_input_path}")

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0 OR NOT stdout MATCHES "bytes_read=6 request_id=1")
  message(FATAL_ERROR
    "single io_uring read should read the complete six-byte input\n"
    "result=${result}\n"
    "stdout=${stdout}\n"
    "stderr=${stderr}"
  )
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${empty_input_path}"
  RESULT_VARIABLE empty_result
  OUTPUT_VARIABLE empty_stdout
  ERROR_VARIABLE empty_stderr
)

if(NOT empty_result EQUAL 0 OR NOT empty_stdout MATCHES "bytes_read=0 request_id=1")
  message(FATAL_ERROR
    "single io_uring read should report EOF for an empty input\n"
    "result=${empty_result}\n"
    "stdout=${empty_stdout}\n"
    "stderr=${empty_stderr}"
  )
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${missing_input_path}"
  RESULT_VARIABLE missing_result
  OUTPUT_VARIABLE missing_stdout
  ERROR_VARIABLE missing_stderr
)

if(missing_result EQUAL 0 OR NOT missing_stderr MATCHES "open input failed")
  message(FATAL_ERROR
    "single io_uring read should reject a missing input\n"
    "result=${missing_result}\n"
    "stdout=${missing_stdout}\n"
    "stderr=${missing_stderr}"
  )
endif()

file(REMOVE "${input_path}" "${empty_input_path}")
