if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TEST_SUFFIX)
set(TEST_DIRECTORY "/tmp/asyncdataloader_stage10_demo_${TEST_SUFFIX}")
set(INPUT_PATH "${TEST_DIRECTORY}/input.bin")
set(SYNC_OUTPUT_PATH "${TEST_DIRECTORY}/sync-output.bin")
set(FALLBACK_OUTPUT_PATH "${TEST_DIRECTORY}/fallback-output.bin")

file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
file(WRITE "${INPUT_PATH}" "01234567ABCxyz~")

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${SYNC_OUTPUT_PATH}"
    --backend=sync
    --block-size=8
    --alignment=8
    --buffers=3
    --queue-depth=1
    --report-ms=0
  RESULT_VARIABLE SYNC_RESULT
  OUTPUT_VARIABLE SYNC_OUTPUT
  ERROR_VARIABLE SYNC_ERROR
)

if(NOT SYNC_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "sync demo failed (${SYNC_RESULT})\nstdout:\n${SYNC_OUTPUT}\nstderr:\n${SYNC_ERROR}"
  )
endif()

file(READ "${SYNC_OUTPUT_PATH}" SYNC_OUTPUT_HEX HEX)
if(NOT SYNC_OUTPUT_HEX STREQUAL "3132333435363738424344797a7b7f")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "sync demo wrote incorrect transformed bytes")
endif()

foreach(EXPECTED_LINE
    "selected_backend=sync"
    "stage=byte_increment"
    "blocks_written=2"
    "bytes_written=15"
    "output_committed=true"
    "verification=passed"
    "pipeline.queue.read_process.depth.peak=1"
    "pipeline.queue.process_write.depth.peak=1")
  string(FIND "${SYNC_OUTPUT}" "${EXPECTED_LINE}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR
      "sync demo output missed '${EXPECTED_LINE}'\n${SYNC_OUTPUT}"
    )
  endif()
endforeach()

execute_process(
  COMMAND
    "${EXECUTABLE_PATH}"
    "${INPUT_PATH}"
    "${FALLBACK_OUTPUT_PATH}"
    --backend=auto
    --disable-uring
    --disable-threadpool
    --block-size=8
    --alignment=8
    --buffers=3
    --queue-depth=1
    --report-ms=0
  RESULT_VARIABLE FALLBACK_RESULT
  OUTPUT_VARIABLE FALLBACK_OUTPUT
  ERROR_VARIABLE FALLBACK_ERROR
)

if(NOT FALLBACK_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "fallback demo failed (${FALLBACK_RESULT})\nstdout:\n${FALLBACK_OUTPUT}\nstderr:\n${FALLBACK_ERROR}"
  )
endif()

file(READ "${FALLBACK_OUTPUT_PATH}" FALLBACK_OUTPUT_HEX HEX)
string(FIND "${FALLBACK_OUTPUT}" "requested_backend=auto" REQUESTED_AT)
string(FIND "${FALLBACK_OUTPUT}" "selected_backend=sync" SELECTED_AT)
string(FIND "${FALLBACK_OUTPUT}" "verification=passed" VERIFIED_AT)
if(NOT FALLBACK_OUTPUT_HEX STREQUAL "3132333435363738424344797a7b7f" OR
   REQUESTED_AT EQUAL -1 OR SELECTED_AT EQUAL -1 OR VERIFIED_AT EQUAL -1)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "Auto fallback did not select sync and preserve correct output\n${FALLBACK_OUTPUT}"
  )
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
