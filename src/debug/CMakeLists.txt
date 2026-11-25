add_library(lxcode_debug STATIC dapclient.cpp)
target_link_libraries(lxcode_debug PUBLIC lxcode_core)
target_include_directories(lxcode_debug PUBLIC "${CMAKE_SOURCE_DIR}/include")
