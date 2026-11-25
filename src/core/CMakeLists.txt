add_library(lxcode_core STATIC
  process.cpp
  filesystem.cpp
  jsonrpc.cpp)

target_include_directories(lxcode_core PUBLIC
  "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(lxcode_core PUBLIC
  HOST_ARCHITECTURE="${CMAKE_SYSTEM_PROCESSOR}")
