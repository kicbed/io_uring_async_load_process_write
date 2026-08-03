if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

get_filename_component(executable_dir "${EXECUTABLE_PATH}" DIRECTORY)
set(input_path "${executable_dir}/stage6_backend_fallback_input.bin")
set(empty_input_path "${executable_dir}/stage6_backend_fallback_empty.bin")
set(missing_input_path "${executable_dir}/stage6_backend_fallback_missing.bin")

file(WRITE "${input_path}" "abcXYZ")
file(WRITE "${empty_input_path}" "")
file(REMOVE "${missing_input_path}")

function(expect_success expected_requested expected_selected)
  execute_process(
    COMMAND "${EXECUTABLE_PATH}" "${input_path}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
  )

  if(NOT result EQUAL 0 OR
     NOT stdout MATCHES "requested_backend=${expected_requested}" OR
     NOT stdout MATCHES "selected_backend=${expected_selected}" OR
     NOT stdout MATCHES "bytes_read=6" OR
     NOT stdout MATCHES "payload=abcXYZ")
    message(FATAL_ERROR
      "backend fallback demo returned an unexpected result\n"
      "command arguments=${ARGN}\n"
      "result=${result}\n"
      "stdout=${stdout}\n"
      "stderr=${stderr}"
    )
  endif()
endfunction()

expect_success("sync" "sync" "--backend=sync")
expect_success("thread_pool" "thread_pool" "--backend=thread_pool")
expect_success("uring" "io_uring" "--backend=uring")
expect_success(
  "auto"
  "thread_pool"
  "--backend=auto"
  "--disable-uring"
)
expect_success(
  "auto"
  "sync"
  "--backend=auto"
  "--disable-uring"
  "--disable-thread-pool"
)

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${empty_input_path}" "--backend=sync"
  RESULT_VARIABLE empty_result
  OUTPUT_VARIABLE empty_stdout
  ERROR_VARIABLE empty_stderr
)
if(NOT empty_result EQUAL 0 OR NOT empty_stdout MATCHES "bytes_read=0")
  message(FATAL_ERROR
    "empty input should be reported as EOF\n"
    "result=${empty_result}\n"
    "stdout=${empty_stdout}\n"
    "stderr=${empty_stderr}"
  )
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${missing_input_path}" "--backend=sync"
  RESULT_VARIABLE missing_result
  OUTPUT_VARIABLE missing_stdout
  ERROR_VARIABLE missing_stderr
)
if(missing_result EQUAL 0 OR NOT missing_stderr MATCHES "open input failed")
  message(FATAL_ERROR
    "missing input should be rejected\n"
    "result=${missing_result}\n"
    "stdout=${missing_stdout}\n"
    "stderr=${missing_stderr}"
  )
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${input_path}" "--backend=unknown"
  RESULT_VARIABLE invalid_result
  OUTPUT_VARIABLE invalid_stdout
  ERROR_VARIABLE invalid_stderr
)
if(invalid_result EQUAL 0 OR NOT invalid_stderr MATCHES "invalid arguments")
  message(FATAL_ERROR
    "unknown backend should be rejected\n"
    "result=${invalid_result}\n"
    "stdout=${invalid_stdout}\n"
    "stderr=${invalid_stderr}"
  )
endif()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${input_path}"
    "--backend=sync"
    "--disable-uring"
  RESULT_VARIABLE contradictory_result
  OUTPUT_VARIABLE contradictory_stdout
  ERROR_VARIABLE contradictory_stderr
)
if(contradictory_result EQUAL 0 OR
   NOT contradictory_stderr MATCHES "only valid with --backend=auto")
  message(FATAL_ERROR
    "contradictory fallback options should be rejected\n"
    "result=${contradictory_result}\n"
    "stdout=${contradictory_stdout}\n"
    "stderr=${contradictory_stderr}"
  )
endif()

file(REMOVE "${input_path}" "${empty_input_path}")
