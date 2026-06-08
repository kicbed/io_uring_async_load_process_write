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
  message(FATAL_ERROR "stage1 simple task demo failed with result '${result}': ${error}")
endif()

if(NOT output MATCHES "Stage 1 simple task demo")
  message(FATAL_ERROR "output does not identify the Stage 1 demo: ${output}")
endif()

if(NOT output MATCHES "before resume")
  message(FATAL_ERROR "output does not show the pre-resume marker: ${output}")
endif()

if(NOT output MATCHES "Hello from coroutine!")
  message(FATAL_ERROR "output does not show coroutine body execution: ${output}")
endif()

if(NOT output MATCHES "after resume")
  message(FATAL_ERROR "output does not show the post-resume marker: ${output}")
endif()

if(NOT output MATCHES "done=true")
  message(FATAL_ERROR "output does not show completed coroutine state: ${output}")
endif()
