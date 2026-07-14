if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

get_filename_component(executable_dir "${EXECUTABLE_PATH}" DIRECTORY)
set(output_path "${executable_dir}/stage4_uring_write_demo_output.bin")
set(missing_dir "${executable_dir}/stage4_uring_write_demo_missing")
set(missing_output_path "${missing_dir}/output.bin")

file(REMOVE "${output_path}")
file(REMOVE_RECURSE "${missing_dir}")

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${output_path}"
  RESULT_VARIABLE write_result
  OUTPUT_VARIABLE write_stdout
  ERROR_VARIABLE write_stderr
)

if(NOT write_result EQUAL 0)
  message(FATAL_ERROR
    "write demo failed with ${write_result}\n"
    "stdout: ${write_stdout}\n"
    "stderr: ${write_stderr}"
  )
endif()

if(NOT write_stdout MATCHES "bytes_written=36 request_id=1 offset=0")
  message(FATAL_ERROR "unexpected write report: ${write_stdout}")
endif()

file(READ "${output_path}" output_contents)
if(NOT output_contents STREQUAL "AsyncDataLoader io_uring write demo\n")
  message(FATAL_ERROR "unexpected output contents: ${output_contents}")
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}" "${missing_output_path}"
  RESULT_VARIABLE open_failure_result
  OUTPUT_VARIABLE open_failure_stdout
  ERROR_VARIABLE open_failure_stderr
)

if(open_failure_result EQUAL 0)
  message(FATAL_ERROR "write demo unexpectedly opened a file in a missing directory")
endif()

if(NOT open_failure_stderr MATCHES "open output failed:")
  message(FATAL_ERROR "unexpected open failure: ${open_failure_stderr}")
endif()

file(REMOVE "${output_path}")
