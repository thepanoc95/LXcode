add_library(lxcode_lsp STATIC client.cpp)
target_link_libraries(lxcode_lsp PUBLIC lxcode_core)
target_include_directories(lxcode_lsp PUBLIC "${CMAKE_SOURCE_DIR}/include")
