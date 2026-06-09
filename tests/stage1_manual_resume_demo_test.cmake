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
  message(FATAL_ERROR "stage1 manual resume demo failed with result '${result}': ${error}")
endif()

if(NOT output MATCHES "AsyncDataLoader")
  message(FATAL_ERROR "output does not identify the project: ${output}")
endif()

if(NOT output MATCHES "Stage 1 manual resume demo")
  message(FATAL_ERROR "output does not identify the Stage 1 manual resume demo: ${output}")
endif()

if(NOT output MATCHES "before first resume")
  message(FATAL_ERROR "output does not show first resume marker: ${output}")
endif()

if(NOT output MATCHES "before co_await")
  message(FATAL_ERROR "output does not show coroutine reached co_await: ${output}")
endif()

if(NOT output MATCHES "after first resume")
  message(FATAL_ERROR "output does not show return to main after first resume: ${output}")
endif()

if(NOT output MATCHES "awaiter has handle=true")
  message(FATAL_ERROR "output does not show awaiter saved the coroutine handle: ${output}")
endif()

if(NOT output MATCHES "before manual resume")
  message(FATAL_ERROR "output does not show manual resume marker: ${output}")
endif()

if(NOT output MATCHES "after co_await")
  message(FATAL_ERROR "output does not show coroutine continued after co_await: ${output}")
endif()

if(NOT output MATCHES "after manual resume")
  message(FATAL_ERROR "output does not show return to main after manual resume: ${output}")
endif()

if(NOT output MATCHES "done=true")
  message(FATAL_ERROR "output does not show completed task state: ${output}")
endif()
