add_library(lxcode_sourcecontrol STATIC git.cpp)
target_link_libraries(lxcode_sourcecontrol PUBLIC lxcode_core)
target_include_directories(lxcode_sourcecontrol PUBLIC "${CMAKE_SOURCE_DIR}/include")
