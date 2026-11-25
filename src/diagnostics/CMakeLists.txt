add_library(lxcode_diagnostics STATIC issues.cpp)
target_link_libraries(lxcode_diagnostics PUBLIC lxcode_core lxcode_project lxcode_toolchains)
target_include_directories(lxcode_diagnostics PUBLIC "${CMAKE_SOURCE_DIR}/include")
