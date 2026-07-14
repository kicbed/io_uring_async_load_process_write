if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

get_filename_component(executable_dir "${EXECUTABLE_PATH}" DIRECTORY)
set(input_path "${executable_dir}/stage4_uring_batch_read_input.bin")
set(empty_input_path "${executable_dir}/stage4_uring_batch_read_empty.bin")
set(missing_input_path "${executable_dir}/stage4_uring_batch_read_missing.bin")

string(REPEAT "x" 8193 input_data)
file(WRITE "${input_path}" "${input_data}")
file(WRITE "${empty_input_path}" "")
file(REMOVE "${missing_input_path}")

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "batched io_uring read failed\n"
    "result=${result}\n"
    "stdout=${stdout}\n"
    "stderr=${stderr}"
  )
endif()

foreach(expected_line IN ITEMS
  "request_id=1 offset=0 bytes_read=4096"
  "request_id=2 offset=4096 bytes_read=4096"
  "request_id=3 offset=8192 bytes_read=1"
  "request_id=4 offset=12288 bytes_read=0"
  "total_bytes_read=8193"
)
  if(NOT stdout MATCHES "${expected_line}")
    message(FATAL_ERROR
      "batched io_uring output is missing: ${expected_line}\n"
      "stdout=${stdout}"
    )
  endif()
endforeach()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${empty_input_path}"
  RESULT_VARIABLE empty_result
  OUTPUT_VARIABLE empty_stdout
  ERROR_VARIABLE empty_stderr
)

if(NOT empty_result EQUAL 0 OR NOT empty_stdout MATCHES "total_bytes_read=0")
  message(FATAL_ERROR
    "batched io_uring read should report EOF for every empty-file request\n"
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
    "batched io_uring read should reject a missing input\n"
    "result=${missing_result}\n"
    "stdout=${missing_stdout}\n"
    "stderr=${missing_stderr}"
  )
endif()

file(REMOVE "${input_path}" "${empty_input_path}")
