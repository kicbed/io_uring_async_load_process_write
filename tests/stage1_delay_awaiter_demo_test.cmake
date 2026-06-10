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
  message(FATAL_ERROR "stage1 delay awaiter demo failed with result '${result}': ${error}")
endif()

if(NOT output MATCHES "AsyncDataLoader")
  message(FATAL_ERROR "output does not identify the project: ${output}")
endif()

if(NOT output MATCHES "Stage 1 delay awaiter demo")
  message(FATAL_ERROR "output does not identify the Stage 1 delay awaiter demo: ${output}")
endif()

if(NOT output MATCHES "before first resume")
  message(FATAL_ERROR "output does not show first resume marker: ${output}")
endif()

if(NOT output MATCHES "before delay co_await")
  message(FATAL_ERROR "output does not show coroutine reached delay co_await: ${output}")
endif()

if(NOT output MATCHES "after first resume")
  message(FATAL_ERROR "output does not show return to main after first resume: ${output}")
endif()

if(NOT output MATCHES "waiting in main")
  message(FATAL_ERROR "output does not show main waiting for delayed resume: ${output}")
endif()

if(NOT output MATCHES "after delay co_await")
  message(FATAL_ERROR "output does not show coroutine resumed after delay: ${output}")
endif()

if(NOT output MATCHES "done=true")
  message(FATAL_ERROR "output does not show completed task state: ${output}")
endif()
