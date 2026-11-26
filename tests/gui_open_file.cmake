# Regression test: opening a source file used to recurse in the Qt highlighter
# and crash the GUI. Keep this test so that path is exercised on every build.
set(_sample "${CMAKE_CURRENT_BINARY_DIR}/gui_open_file_sample.c")
file(WRITE "${_sample}" "int main(void) { return 0; }\n")

execute_process(
  COMMAND "${GUI}" --open-file "${_sample}" --smoke-test
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr)

if(NOT _result EQUAL 0)
  message(FATAL_ERROR "lxcode-gui crashed while opening a file (exit ${_result}):\n${_stderr}")
endif()
