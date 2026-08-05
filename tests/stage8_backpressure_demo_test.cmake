if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(expected_output
  "pool_capacity=2\nqueue_capacity=1\nproducer_blocked_while_queue_full=true\nconsumed_markers=1,2\nbuffers_returned=2"
)

if(NOT result EQUAL 0 OR NOT stdout STREQUAL expected_output)
  message(FATAL_ERROR
    "backpressure demo returned an unexpected result\n"
    "result=${result}\n"
    "stdout=${stdout}\n"
    "stderr=${stderr}"
  )
endif()
