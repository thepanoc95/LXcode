add_library(lxcode_xcodesdk STATIC
  linuxbundle.cpp)

target_link_libraries(lxcode_xcodesdk PUBLIC lxcode_core lxcode_project)
target_include_directories(lxcode_xcodesdk PUBLIC "${CMAKE_SOURCE_DIR}/include")
