foreach(REQUIRED_VARIABLE PYTHON_EXECUTABLE SCRIPT_PATH)
  if(NOT DEFINED ${REQUIRED_VARIABLE})
    message(FATAL_ERROR "${REQUIRED_VARIABLE} is required")
  endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TEST_SUFFIX)
set(TEST_DIRECTORY "/tmp/asyncdataloader_stage11_profile_${TEST_SUFFIX}")
set(OUTPUT_DIRECTORY "${TEST_DIRECTORY}/evidence")
set(FAKE_PROFILER "${TEST_DIRECTORY}/fake-profiler.sh")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${FAKE_PROFILER}" [=[#!/bin/sh
profile_output=
while [ "$#" -gt 0 ]; do
  if [ "$1" = "-o" ]; then
    profile_output=$2
    shift 2
  elif [ "$1" = "--" ]; then
    shift
    break
  else
    shift
  fi
done
printf 'synthetic profiler evidence\n' > "$profile_output"
exec "$@"
]=])
file(CHMOD "${FAKE_PROFILER}"
  PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)

foreach(TOOL strace perf)
  set(LABEL "${TOOL}-success")
  execute_process(
    COMMAND
      "${PYTHON_EXECUTABLE}"
      "${SCRIPT_PATH}"
      --tool "${TOOL}"
      --output-directory "${OUTPUT_DIRECTORY}"
      --label "${LABEL}"
      --environment-id ctest-synthetic
      --strace-executable "${FAKE_PROFILER}"
      --perf-executable "${FAKE_PROFILER}"
      --
      "${CMAKE_COMMAND}" -E echo child-ok
    RESULT_VARIABLE RESULT
    OUTPUT_VARIABLE STDOUT
    ERROR_VARIABLE STDERR
  )
  if(NOT RESULT EQUAL 0 OR
     NOT STDOUT MATCHES "status=complete tool=${TOOL}")
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR
      "${TOOL} capture failed (${RESULT})\n"
      "stdout:\n${STDOUT}\nstderr:\n${STDERR}"
    )
  endif()

  set(RESULT_DIRECTORY "${OUTPUT_DIRECTORY}/${LABEL}")
  file(READ "${RESULT_DIRECTORY}/manifest.txt" MANIFEST)
  file(READ "${RESULT_DIRECTORY}/command.stdout.txt" CHILD_STDOUT)
  if(NOT MANIFEST MATCHES "status=complete" OR
     NOT MANIFEST MATCHES "environment_id=ctest-synthetic" OR
     NOT MANIFEST MATCHES "tool=${TOOL}" OR
     NOT MANIFEST MATCHES "exit_code=0" OR
     NOT MANIFEST MATCHES "timing_scope=diagnostic_only_not_benchmark_csv" OR
     NOT CHILD_STDOUT MATCHES "child-ok")
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR "${TOOL} evidence content was incomplete")
  endif()

  if(TOOL STREQUAL "strace")
    set(PROFILE_FILE "strace-summary.txt")
  else()
    set(PROFILE_FILE "perf-stat.csv")
  endif()
  file(READ "${RESULT_DIRECTORY}/${PROFILE_FILE}" PROFILE_CONTENT)
  if(NOT PROFILE_CONTENT MATCHES "synthetic profiler evidence")
    file(REMOVE_RECURSE "${TEST_DIRECTORY}")
    message(FATAL_ERROR "${TOOL} raw profiler output was missing")
  endif()
endforeach()

execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SCRIPT_PATH}"
    --tool strace
    --output-directory "${OUTPUT_DIRECTORY}"
    --label failed-command
    --environment-id ctest-synthetic
    --strace-executable "${FAKE_PROFILER}"
    --
    /bin/sh -c "exit 7"
  RESULT_VARIABLE FAILED_RESULT
  OUTPUT_VARIABLE FAILED_STDOUT
  ERROR_VARIABLE FAILED_STDERR
)
if(NOT FAILED_RESULT EQUAL 2 OR
   NOT FAILED_STDOUT MATCHES "status=failed tool=strace")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "failed child did not preserve the failure contract (${FAILED_RESULT})\n"
    "stdout:\n${FAILED_STDOUT}\nstderr:\n${FAILED_STDERR}"
  )
endif()
file(READ "${OUTPUT_DIRECTORY}/failed-command/manifest.txt" FAILED_MANIFEST)
if(NOT FAILED_MANIFEST MATCHES "status=failed" OR
   NOT FAILED_MANIFEST MATCHES "exit_code=7")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "failed child evidence was not published")
endif()

execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SCRIPT_PATH}"
    --tool strace
    --output-directory "${OUTPUT_DIRECTORY}"
    --label missing-profile-output
    --environment-id ctest-synthetic
    --strace-executable /bin/true
    --
    "${CMAKE_COMMAND}" -E echo must-not-be-presented-as-profiled
  RESULT_VARIABLE MISSING_PROFILE_RESULT
  OUTPUT_VARIABLE MISSING_PROFILE_STDOUT
  ERROR_VARIABLE MISSING_PROFILE_STDERR
)
if(NOT MISSING_PROFILE_RESULT EQUAL 2 OR
   NOT MISSING_PROFILE_STDOUT MATCHES "status=failed tool=strace")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR
    "missing profiler output was accepted (${MISSING_PROFILE_RESULT})\n"
    "stdout:\n${MISSING_PROFILE_STDOUT}\n"
    "stderr:\n${MISSING_PROFILE_STDERR}"
  )
endif()
file(READ
  "${OUTPUT_DIRECTORY}/missing-profile-output/manifest.txt"
  MISSING_PROFILE_MANIFEST
)
if(NOT MISSING_PROFILE_MANIFEST MATCHES "status=failed" OR
   NOT MISSING_PROFILE_MANIFEST MATCHES "profile_output_present=false")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "missing profiler output failure was not recorded")
endif()

execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${SCRIPT_PATH}"
    --tool strace
    --output-directory "${OUTPUT_DIRECTORY}"
    --label strace-success
    --environment-id ctest-synthetic
    --strace-executable "${FAKE_PROFILER}"
    --
    "${CMAKE_COMMAND}" -E echo must-not-run
  RESULT_VARIABLE DUPLICATE_RESULT
  OUTPUT_QUIET
  ERROR_VARIABLE DUPLICATE_STDERR
)
if(DUPLICATE_RESULT EQUAL 0 OR
   NOT DUPLICATE_STDERR MATCHES "evidence directory already exists")
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "existing profiler evidence could be overwritten")
endif()

file(GLOB LEFTOVER_TEMP_DIRECTORIES "${OUTPUT_DIRECTORY}/.*.tmp.*")
if(LEFTOVER_TEMP_DIRECTORIES)
  file(REMOVE_RECURSE "${TEST_DIRECTORY}")
  message(FATAL_ERROR "profile capture left temporary directories")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
