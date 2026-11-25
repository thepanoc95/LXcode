add_library(lxcode_toolchains STATIC toolchain.cpp)
target_link_libraries(lxcode_toolchains PUBLIC lxcode_core)
target_include_directories(lxcode_toolchains PUBLIC "${CMAKE_SOURCE_DIR}/include")
