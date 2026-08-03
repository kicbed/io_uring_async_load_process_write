if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

get_filename_component(executable_dir "${EXECUTABLE_PATH}" DIRECTORY)
set(input_path "${executable_dir}/stage5_async_read_input.bin")
set(empty_input_path "${executable_dir}/stage5_async_read_empty.bin")
set(missing_input_path "${executable_dir}/stage5_async_read_missing.bin")

file(WRITE "${input_path}" "abcXYZ")
file(WRITE "${empty_input_path}" "")
file(REMOVE "${missing_input_path}")

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0 OR
   NOT stdout MATCHES "bytes_read=6" OR
   NOT stdout MATCHES "payload=abcXYZ")
  message(FATAL_ERROR
    "coroutine io_uring read should return the input payload\n"
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

if(NOT empty_result EQUAL 0 OR
   NOT empty_stdout MATCHES "bytes_read=0")
  message(FATAL_ERROR
    "coroutine io_uring read should report EOF for an empty input\n"
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

if(missing_result EQUAL 0 OR
   NOT missing_stderr MATCHES "open input failed")
  message(FATAL_ERROR
    "coroutine io_uring read should reject a missing input\n"
    "result=${missing_result}\n"
    "stdout=${missing_stdout}\n"
    "stderr=${missing_stderr}"
  )
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${executable_dir}"
  RESULT_VARIABLE directory_result
  OUTPUT_VARIABLE directory_stdout
  ERROR_VARIABLE directory_stderr
)

if(directory_result EQUAL 0 OR
   NOT directory_stderr MATCHES "async read failed")
  message(FATAL_ERROR
    "negative CQE result should propagate through Task\n"
    "result=${directory_result}\n"
    "stdout=${directory_stdout}\n"
    "stderr=${directory_stderr}"
  )
endif()

file(REMOVE "${input_path}" "${empty_input_path}")
