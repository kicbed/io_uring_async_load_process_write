if(NOT DEFINED EXECUTABLE_PATH)
  message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()

execute_process(
  COMMAND "${EXECUTABLE_PATH}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "stage0 executable failed with result '${result}': ${error}")
endif()

if(NOT output MATCHES "AsyncDataLoader")
  message(FATAL_ERROR "output does not identify the project: ${output}")
endif()

if(NOT output MATCHES "Stage 0")
  message(FATAL_ERROR "output does not identify the current stage: ${output}")
endif()
