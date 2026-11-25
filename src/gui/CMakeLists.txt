file(GLOB LXCODE_TS_RUNTIME "${CMAKE_SOURCE_DIR}/third_party/tree-sitter/runtime/*.c")

add_library(lxcode_treesitter STATIC
  "${CMAKE_SOURCE_DIR}/third_party/tree-sitter/c/src/parser.c"
  ${LXCODE_TS_RUNTIME})
target_include_directories(lxcode_treesitter PUBLIC
  "${CMAKE_SOURCE_DIR}/third_party/tree-sitter/include"
  "${CMAKE_SOURCE_DIR}/third_party/tree-sitter/c/src"
  "${CMAKE_SOURCE_DIR}/third_party/tree-sitter/runtime")

add_library(lxcode_gui STATIC
  mainwindow.cpp
  codeeditor.cpp
  settingsdialog.cpp
  theme.cpp)
set_target_properties(lxcode_gui PROPERTIES AUTOMOC ON)
target_include_directories(lxcode_gui PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/third_party")
target_compile_definitions(lxcode_gui PUBLIC
  LXCODE_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources")
target_link_libraries(lxcode_gui PUBLIC
  Qt6::Core Qt6::Widgets Qt6::Gui Qt6::Network Qt6::Concurrent
  lxcode_treesitter
  lxcode_core lxcode_project lxcode_toolchains lxcode_build
  lxcode_sourcecontrol lxcode_lsp lxcode_debug lxcode_config lxcode_diagnostics)
