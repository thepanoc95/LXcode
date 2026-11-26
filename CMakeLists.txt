cmake_minimum_required(VERSION 3.22)
project(LXcode VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LXCODE_BUILD_GUI "Build the Qt 6 desktop interface" ON)
option(LXCODE_BUILD_TESTS "Build the automated test suite" ON)
option(LXCODE_USE_SYSTEM_DEPS "Use system-installed dependencies" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/third_party")

add_subdirectory(src/core)
add_subdirectory(src/project)
add_subdirectory(src/xcodesdk)
add_subdirectory(src/toolchains)
add_subdirectory(src/build)
add_subdirectory(src/sourcecontrol)
add_subdirectory(src/lsp)
add_subdirectory(src/debug)
add_subdirectory(src/config)
add_subdirectory(src/diagnostics)

add_executable(lxcode-cli src/cli/main.cpp)
target_link_libraries(lxcode-cli PRIVATE
  lxcode_core lxcode_project lxcode_xcodesdk lxcode_toolchains lxcode_build
  lxcode_sourcecontrol lxcode_config lxcode_diagnostics)
set_target_properties(lxcode-cli PROPERTIES OUTPUT_NAME lxcode)

if(LXCODE_BUILD_GUI)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Gui Network Concurrent)
  add_subdirectory(src/gui)
  add_executable(lxcode-gui src/gui/main.cpp)
  target_link_libraries(lxcode-gui PRIVATE lxcode_gui)
  set_target_properties(lxcode-gui PROPERTIES OUTPUT_NAME lxcode-gui)
endif()

if(LXCODE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS lxcode-cli RUNTIME DESTINATION bin)
if(LXCODE_BUILD_GUI)
  install(TARGETS lxcode-gui RUNTIME DESTINATION bin)
  install(DIRECTORY resources/ DESTINATION share/lxcode)
endif()
