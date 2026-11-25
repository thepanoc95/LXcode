add_library(lxcode_project STATIC
  pbxproj.cpp
  xml.cpp
  plist.cpp
  xcconfig.cpp
  workspace.cpp
  scheme.cpp
  xcodeproject.cpp)

target_link_libraries(lxcode_project PUBLIC lxcode_core)
target_include_directories(lxcode_project PUBLIC "${CMAKE_SOURCE_DIR}/include")
