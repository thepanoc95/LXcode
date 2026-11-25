execute_process(
  COMMAND "${CLI}" doctor
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
  RESULT_VARIABLE result)

message(STATUS "lxcode doctor output:\n${output}${errors}")
if(result STREQUAL "0")
  return()
endif()
# Doctor intentionally reports a non-zero exit code when no Apple SDK is
# installed. As long as the command ran and produced a report, the CLI works.
if(output STREQUAL "" AND errors STREQUAL "")
  message(FATAL_ERROR "lxcode doctor produced no output")
endif()
