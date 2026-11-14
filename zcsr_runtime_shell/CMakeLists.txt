cmake_minimum_required(VERSION 3.16)
project(zcsr_runtime_shell LANGUAGES C)

add_library(zcsr_runtime_shell STATIC src/runtime_shell.c)

target_include_directories(zcsr_runtime_shell PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ZCSR_ENGINE_DIR}/include
    ${ZCSR_ENGINE_DIR}/third_party/glad/include
    ${ZCSR_UIM_DIR}/include
    ${ZCSR_UIM_DIR}/zcsr_ui_page_manager/include)

target_link_libraries(zcsr_runtime_shell PUBLIC
    zcsr_ui_page_manager
    zcsr_ui
    zcsr_glrender
    zcsr_platform
    zcsr_input
    zcsr_inputext
    zcsr_audio
    zcsr_audiomix
    zcsr_core)

if(MSVC)
  target_compile_options(zcsr_runtime_shell PRIVATE /W4 /WX)
endif()
