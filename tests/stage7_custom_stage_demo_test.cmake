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
  "input=1,2,3\nregistered_stage=custom_affine\nstage_count=1\noutput=3,5,7"
)

if(NOT result EQUAL 0 OR NOT stdout STREQUAL expected_output)
  message(FATAL_ERROR
    "custom stage demo returned an unexpected result\n"
    "result=${result}\n"
    "stdout=${stdout}\n"
    "stderr=${stderr}"
  )
endif()
