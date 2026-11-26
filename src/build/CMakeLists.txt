add_library(lxcode_build STATIC buildsystem.cpp)
target_link_libraries(lxcode_build PUBLIC lxcode_core lxcode_project lxcode_toolchains)
target_include_directories(lxcode_build PUBLIC "${CMAKE_SOURCE_DIR}/include")
