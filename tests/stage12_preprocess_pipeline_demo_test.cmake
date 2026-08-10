if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TEST_SUFFIX)
set(TEST_DIRECTORY "/tmp/asyncdataloader_stage12_demo_${TEST_SUFFIX}")
set(INPUT_PATH "${TEST_DIRECTORY}/input.bin")
set(OUTPUT_PATH "${TEST_DIRECTORY}/output.bin")
set(JSON_PATH "${TEST_DIRECTORY}/metrics.json")
set(COLLISION_OUTPUT_PATH "${TEST_DIRECTORY}/collision-output.bin")

file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
file(WRITE "${INPUT_PATH}" "01234567ABCxyz~")
file(WRITE "${JSON_PATH}" "old metrics must be replaced")

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${OUTPUT_PATH}"
    --backend=sync
    --block-size=8
    --alignment=8
    --buffers=3
    --queue-depth=1
    --report-ms=0
    "--metrics-json=${JSON_PATH}"
  RESULT_VARIABLE RUN_RESULT
  OUTPUT_VARIABLE RUN_OUTPUT
  ERROR_VARIABLE RUN_ERROR
)

if(NOT RUN_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "Stage 12 demo failed (${RUN_RESULT})\nstdout:\n${RUN_OUTPUT}\nstderr:\n${RUN_ERROR}"
  )
endif()

file(READ "${OUTPUT_PATH}" OUTPUT_HEX HEX)
if(NOT OUTPUT_HEX STREQUAL "3132333435363738424344797a7b7f")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "Stage 12 demo wrote incorrect transformed bytes")
endif()

foreach(EXPECTED_TEXT
    "AsyncDataLoader"
    "Backend        : sync -> sync"
    "Pipeline completed"
    "Output commit  : yes"
    "Verification   : yes"
    "Metrics JSON     : ${JSON_PATH}"
    "Machine-readable metrics"
    "selected_backend=sync"
    "verification=passed")
  string(FIND "${RUN_OUTPUT}" "${EXPECTED_TEXT}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR
      "Stage 12 terminal output missed '${EXPECTED_TEXT}'\n${RUN_OUTPUT}"
    )
  endif()
endforeach()

if(NOT EXISTS "${JSON_PATH}")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "Stage 12 did not publish metrics JSON")
endif()
file(READ "${JSON_PATH}" JSON_CONTENT)

string(JSON SCHEMA_VERSION ERROR_VARIABLE JSON_ERROR
  GET "${JSON_CONTENT}" schema_version)
if(JSON_ERROR OR NOT SCHEMA_VERSION EQUAL 1)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "metrics JSON schema_version is invalid: ${JSON_ERROR}")
endif()

string(JSON JSON_STATUS GET "${JSON_CONTENT}" status)
string(JSON JSON_BACKEND GET "${JSON_CONTENT}" run selected_backend)
string(JSON JSON_STAGE GET "${JSON_CONTENT}" run stage)
string(JSON JSON_BYTES GET "${JSON_CONTENT}" result bytes_written)
string(JSON JSON_VERIFICATION GET "${JSON_CONTENT}" result verification)
string(JSON COUNTER_COUNT LENGTH "${JSON_CONTENT}" metrics counters)
string(JSON GAUGE_COUNT LENGTH "${JSON_CONTENT}" metrics gauges)
string(JSON HISTOGRAM_COUNT LENGTH "${JSON_CONTENT}" metrics histograms)

if(NOT JSON_STATUS STREQUAL "complete" OR
   NOT JSON_BACKEND STREQUAL "sync" OR
   NOT JSON_STAGE STREQUAL "byte_increment" OR
   NOT JSON_BYTES EQUAL 15 OR
   NOT JSON_VERIFICATION STREQUAL "passed" OR
   COUNTER_COUNT LESS 6 OR
   GAUGE_COUNT LESS 3 OR
   HISTOGRAM_COUNT LESS 4)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "metrics JSON content is incomplete or incorrect")
endif()

file(GLOB JSON_TEMPORARIES "${TEST_DIRECTORY}/.metrics.json.tmp.*")
if(JSON_TEMPORARIES)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "metrics JSON publication left a temporary file")
endif()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${COLLISION_OUTPUT_PATH}"
    --backend=sync
    --block-size=8
    --alignment=8
    --buffers=3
    --queue-depth=1
    --report-ms=0
    "--metrics-json=${COLLISION_OUTPUT_PATH}"
  RESULT_VARIABLE COLLISION_RESULT
  OUTPUT_VARIABLE COLLISION_OUTPUT
  ERROR_VARIABLE COLLISION_ERROR
)
if(NOT COLLISION_RESULT EQUAL 2 OR EXISTS "${COLLISION_OUTPUT_PATH}")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "JSON/output path collision was not rejected before writing\n"
    "stdout:\n${COLLISION_OUTPUT}\nstderr:\n${COLLISION_ERROR}"
  )
endif()
string(FIND
  "${COLLISION_ERROR}"
  "metrics JSON path must differ from pipeline output"
  COLLISION_MESSAGE_AT
)
if(COLLISION_MESSAGE_AT EQUAL -1)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "JSON/output collision reported the wrong error")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
