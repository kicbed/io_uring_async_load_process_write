foreach(REQUIRED_VARIABLE
    PYTHON_EXECUTABLE
    SCRIPT_PATH
    PIPELINE_EXECUTABLE)
  if(NOT DEFINED ${REQUIRED_VARIABLE})
    message(FATAL_ERROR "${REQUIRED_VARIABLE} is required")
  endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TEST_SUFFIX)
set(TEST_DIRECTORY "/tmp/asyncdataloader_stage11_sweep_${TEST_SUFFIX}")
set(OUTPUT_DIRECTORY "${TEST_DIRECTORY}/outputs")
set(INPUT_PATH "${TEST_DIRECTORY}/input.bin")
set(RESULT_CSV "${TEST_DIRECTORY}/results.csv")

file(MAKE_DIRECTORY "${TEST_DIRECTORY}" "${OUTPUT_DIRECTORY}")
string(REPEAT "a" 12289 INPUT_DATA)
file(WRITE "${INPUT_PATH}" "${INPUT_DATA}")

execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SCRIPT_PATH}"
    --executable "${PIPELINE_EXECUTABLE}"
    --time-executable /usr/bin/time
    --input "${INPUT_PATH}"
    --output-directory "${OUTPUT_DIRECTORY}"
    --csv "${RESULT_CSV}"
    --environment-id ctest
    --block-sizes 4096,8192
    --buffers 3
    --queue-depths 1,2
    --backends sync,threadpool
    --thread-workers 1
    --iterations 1
    --rss-limit-mib 1024
  RESULT_VARIABLE RESULT
  OUTPUT_VARIABLE STDOUT
  ERROR_VARIABLE STDERR
)

if(NOT RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "Stage 11.3 parameter sweep failed (${RESULT})\n"
    "stdout:\n${STDOUT}\nstderr:\n${STDERR}"
  )
endif()
if(NOT STDOUT MATCHES "status=complete runs=8")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "parameter sweep did not report eight completed runs")
endif()

file(READ "${RESULT_CSV}" CSV_CONTENT)
foreach(EXPECTED_TEXT
    "environment_id,input_path,input_bytes,requested_backend,selected_backend"
    "ctest,${INPUT_PATH},12289,sync,sync,4096,3,1,1,1,"
    "ctest,${INPUT_PATH},12289,sync,sync,4096,3,2,1,1,"
    "ctest,${INPUT_PATH},12289,sync,sync,8192,3,1,1,1,"
    "ctest,${INPUT_PATH},12289,sync,sync,8192,3,2,1,1,"
    "ctest,${INPUT_PATH},12289,threadpool,thread_pool,4096,3,1,1,1,"
    "ctest,${INPUT_PATH},12289,threadpool,thread_pool,4096,3,2,1,1,"
    "ctest,${INPUT_PATH},12289,threadpool,thread_pool,8192,3,1,1,1,"
    "ctest,${INPUT_PATH},12289,threadpool,thread_pool,8192,3,2,1,1,")
  string(FIND "${CSV_CONTENT}" "${EXPECTED_TEXT}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR
      "parameter CSV missed '${EXPECTED_TEXT}'\n${CSV_CONTENT}"
    )
  endif()
endforeach()
string(REGEX MATCHALL "ctest,[^\n]+" RESULT_ROWS "${CSV_CONTENT}")
list(LENGTH RESULT_ROWS RESULT_ROW_COUNT)
if(NOT RESULT_ROW_COUNT EQUAL 8 OR
   NOT CSV_CONTENT MATCHES ",1024,true,passed")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "parameter CSV row count/RSS/verification was wrong")
endif()

file(GLOB LEFTOVER_SCRATCH_FILES
  "${OUTPUT_DIRECTORY}/.stage11-sweep-output-*"
  "${OUTPUT_DIRECTORY}/.stage11-sweep-rss-*"
)
if(LEFTOVER_SCRATCH_FILES)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "parameter sweep left temporary scratch files")
endif()

set(LIMIT_CSV "${TEST_DIRECTORY}/limit.csv")
execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SCRIPT_PATH}"
    --executable "${PIPELINE_EXECUTABLE}"
    --time-executable /usr/bin/time
    --input "${INPUT_PATH}"
    --output-directory "${OUTPUT_DIRECTORY}"
    --csv "${LIMIT_CSV}"
    --environment-id ctest-limit
    --block-sizes 4096
    --buffers 3
    --queue-depths 1
    --backends sync
    --thread-workers 1
    --iterations 1
    --rss-limit-mib 1
  RESULT_VARIABLE LIMIT_RESULT
  OUTPUT_VARIABLE LIMIT_STDOUT
  ERROR_VARIABLE LIMIT_STDERR
)
if(NOT LIMIT_RESULT EQUAL 3 OR
   NOT LIMIT_STDOUT MATCHES "status=complete runs=1" OR
   NOT LIMIT_STDERR MATCHES "rss limit exceeded by 1 sample" )
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "RSS limit failure contract was wrong (${LIMIT_RESULT})\n"
    "stdout:\n${LIMIT_STDOUT}\nstderr:\n${LIMIT_STDERR}"
  )
endif()
file(READ "${LIMIT_CSV}" LIMIT_CSV_CONTENT)
if(NOT LIMIT_CSV_CONTENT MATCHES ",1,false,passed")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "RSS limit failure was not preserved in CSV")
endif()

set(SENTINEL_CSV "${TEST_DIRECTORY}/sentinel.csv")
file(WRITE "${SENTINEL_CSV}" "keep-existing-results")
execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SCRIPT_PATH}"
    --executable "${PIPELINE_EXECUTABLE}"
    --input "${INPUT_PATH}"
    --output-directory "${OUTPUT_DIRECTORY}"
    --csv "${SENTINEL_CSV}"
    --environment-id invalid
    --block-sizes 7
    --buffers 3
    --queue-depths 1
    --backends sync
  RESULT_VARIABLE INVALID_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)
file(READ "${SENTINEL_CSV}" SENTINEL_CONTENT)
if(INVALID_RESULT EQUAL 0 OR
   NOT SENTINEL_CONTENT STREQUAL "keep-existing-results")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "invalid sweep must not overwrite an existing CSV")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
