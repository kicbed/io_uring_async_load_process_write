foreach(REQUIRED_VARIABLE
    PYTHON_EXECUTABLE
    SCRIPT_PATH
    FIXTURE_PATH
    SWEEP_SCRIPT_PATH
    PIPELINE_EXECUTABLE)
  if(NOT DEFINED ${REQUIRED_VARIABLE})
    message(FATAL_ERROR "${REQUIRED_VARIABLE} is required")
  endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TEST_SUFFIX)
set(TEST_DIRECTORY "/tmp/asyncdataloader_stage11_analysis_${TEST_SUFFIX}")
set(OUTPUT_DIRECTORY "${TEST_DIRECTORY}/output")
set(LIVE_OUTPUT_DIRECTORY "${TEST_DIRECTORY}/live-output")
set(LIVE_ANALYSIS_DIRECTORY "${TEST_DIRECTORY}/live-analysis")
set(LIVE_INPUT "${TEST_DIRECTORY}/live-input.bin")
set(LIVE_CSV "${TEST_DIRECTORY}/live.csv")
file(MAKE_DIRECTORY
  "${OUTPUT_DIRECTORY}"
  "${LIVE_OUTPUT_DIRECTORY}"
  "${LIVE_ANALYSIS_DIRECTORY}"
)

string(REPEAT "z" 8193 LIVE_INPUT_DATA)
file(WRITE "${LIVE_INPUT}" "${LIVE_INPUT_DATA}")
execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SWEEP_SCRIPT_PATH}"
    --executable "${PIPELINE_EXECUTABLE}"
    --input "${LIVE_INPUT}"
    --output-directory "${LIVE_OUTPUT_DIRECTORY}"
    --csv "${LIVE_CSV}"
    --environment-id ctest-live-chain
    --block-sizes 4096
    --buffers 3
    --queue-depths 1
    --backends sync,threadpool
    --thread-workers 1
    --iterations 1
    --rss-limit-mib 1024
  RESULT_VARIABLE LIVE_SWEEP_RESULT
  OUTPUT_VARIABLE LIVE_SWEEP_STDOUT
  ERROR_VARIABLE LIVE_SWEEP_STDERR
)
if(NOT LIVE_SWEEP_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "live Stage 11.3 input generation failed (${LIVE_SWEEP_RESULT})\n"
    "stdout:\n${LIVE_SWEEP_STDOUT}\nstderr:\n${LIVE_SWEEP_STDERR}"
  )
endif()

execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SCRIPT_PATH}"
    --input-csv "${LIVE_CSV}"
    --output-directory "${LIVE_ANALYSIS_DIRECTORY}"
    --minimum-samples 1
  RESULT_VARIABLE LIVE_ANALYSIS_RESULT
  OUTPUT_VARIABLE LIVE_ANALYSIS_STDOUT
  ERROR_VARIABLE LIVE_ANALYSIS_STDERR
)
if(NOT LIVE_ANALYSIS_RESULT EQUAL 0 OR
   NOT LIVE_ANALYSIS_STDOUT MATCHES "status=complete samples=2 groups=2")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "live Stage 11.3 -> 11.4 chain failed (${LIVE_ANALYSIS_RESULT})\n"
    "stdout:\n${LIVE_ANALYSIS_STDOUT}\nstderr:\n${LIVE_ANALYSIS_STDERR}"
  )
endif()

execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SCRIPT_PATH}"
    --input-csv "${FIXTURE_PATH}"
    --output-directory "${OUTPUT_DIRECTORY}"
    --minimum-samples 3
    --title "Synthetic Stage 11.4 Test"
  RESULT_VARIABLE RESULT
  OUTPUT_VARIABLE STDOUT
  ERROR_VARIABLE STDERR
)

if(NOT RESULT EQUAL 0 OR
   NOT STDOUT MATCHES "status=complete samples=9 groups=3")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "Stage 11.4 analysis failed (${RESULT})\n"
    "stdout:\n${STDOUT}\nstderr:\n${STDERR}"
  )
endif()

foreach(EXPECTED_FILE summary.csv throughput.svg peak_rss.svg analysis.md)
  if(NOT EXISTS "${OUTPUT_DIRECTORY}/${EXPECTED_FILE}")
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR "analysis did not create ${EXPECTED_FILE}")
  endif()
endforeach()

file(READ "${OUTPUT_DIRECTORY}/summary.csv" SUMMARY_CONTENT)
string(REGEX MATCHALL "ctest-synthetic,[^\n]+" SUMMARY_ROWS "${SUMMARY_CONTENT}")
list(LENGTH SUMMARY_ROWS SUMMARY_ROW_COUNT)
if(NOT SUMMARY_ROW_COUNT EQUAL 3 OR
   NOT SUMMARY_CONTENT MATCHES "sync,sync,1048576,3,2,2,3145728,3,11.000,11.000,12.000,90.909" OR
   NOT SUMMARY_CONTENT MATCHES "threadpool,thread_pool,1048576,3,2,2,3145728,3,8.000,8.000,8.500,125.000" OR
   NOT SUMMARY_CONTENT MATCHES ",1.375")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "summary statistics were wrong\n${SUMMARY_CONTENT}")
endif()

file(READ "${OUTPUT_DIRECTORY}/analysis.md" REPORT_CONTENT)
foreach(EXPECTED_TEXT
    "fastest observed mechanism was **ThreadPool**"
    "io_uring was not the fastest observed mechanism"
    "No cause is claimed without system-level evidence"
    "Auto rows record fallback policy behavior")
  string(FIND "${REPORT_CONTENT}" "${EXPECTED_TEXT}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR "analysis report missed '${EXPECTED_TEXT}'")
  endif()
endforeach()

foreach(SVG_FILE throughput.svg peak_rss.svg)
  file(READ "${OUTPUT_DIRECTORY}/${SVG_FILE}" SVG_CONTENT)
  if(NOT SVG_CONTENT MATCHES "<svg" OR
     NOT SVG_CONTENT MATCHES "Synthetic Stage 11.4 Test")
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR "${SVG_FILE} was not a valid generated chart")
  endif()
endforeach()

file(READ "${FIXTURE_PATH}" INVALID_CONTENT)
string(REPLACE ",passed" ",failed" INVALID_CONTENT "${INVALID_CONTENT}")
set(INVALID_CSV "${TEST_DIRECTORY}/invalid.csv")
file(WRITE "${INVALID_CSV}" "${INVALID_CONTENT}")
file(WRITE "${OUTPUT_DIRECTORY}/summary.csv" "keep-existing-summary")
execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SCRIPT_PATH}"
    --input-csv "${INVALID_CSV}"
    --output-directory "${OUTPUT_DIRECTORY}"
    --minimum-samples 3
  RESULT_VARIABLE INVALID_RESULT
  OUTPUT_QUIET
  ERROR_VARIABLE INVALID_STDERR
)
file(READ "${OUTPUT_DIRECTORY}/summary.csv" SENTINEL_CONTENT)
if(INVALID_RESULT EQUAL 0 OR
   NOT INVALID_STDERR MATCHES "output verification did not pass" OR
   NOT SENTINEL_CONTENT STREQUAL "keep-existing-summary")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "invalid evidence overwrote an existing analysis")
endif()

file(GLOB LEFTOVER_TEMP_FILES "${OUTPUT_DIRECTORY}/.*.tmp.*")
if(LEFTOVER_TEMP_FILES)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "analysis left temporary publication files")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
