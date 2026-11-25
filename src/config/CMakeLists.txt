add_library(lxcode_config STATIC settings.cpp)
target_link_libraries(lxcode_config PUBLIC lxcode_core)
target_include_directories(lxcode_config PUBLIC "${CMAKE_SOURCE_DIR}/include")
