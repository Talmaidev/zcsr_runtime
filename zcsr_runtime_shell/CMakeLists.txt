cmake_minimum_required(VERSION 3.16)
project(zcsr_runtime_shell LANGUAGES C)

add_library(zcsr_runtime_shell STATIC src/runtime_shell.c)

option(ZCSR_RUNTIME_WITH_SETTINGS_PAGE
       "Build the optional UIM Page Manager settings overlay into the runtime shell"
       OFF)

target_include_directories(zcsr_runtime_shell PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ZCSR_ENGINE_DIR}/include
    ${ZCSR_ENGINE_DIR}/third_party/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../zcsr_frame_loop/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../zcsr_task_scheduler/include)

target_link_libraries(zcsr_runtime_shell PUBLIC
    zcsr_frame_loop
    zcsr_task_scheduler
    zcsr_ui
    zcsr_glrender
    zcsr_platform
    zcsr_input
    zcsr_inputext
    zcsr_audio
    zcsr_audiomix
    zcsr_core)

if(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
  target_include_directories(zcsr_runtime_shell PRIVATE
      ${ZCSR_UIM_DIR}/include
      ${ZCSR_UIM_DIR}/zcsr_ui_page_manager/include)
  target_link_libraries(zcsr_runtime_shell PRIVATE zcsr_ui_page_manager)
  target_compile_definitions(zcsr_runtime_shell PRIVATE
      ZCSR_RUNTIME_WITH_SETTINGS_PAGE=1)
endif()

if(MSVC)
  target_compile_options(zcsr_runtime_shell PRIVATE /W4 /WX)
endif()
