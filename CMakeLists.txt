cmake_minimum_required(VERSION 3.16)
project(zcsr_runtime LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8)
endif()

# Runtime is an engine-family library, not part of zcsr core.
# Point these at the real repos; defaults use the engine repo root.
set(ZCSR_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../zero-coupling-signal-router" CACHE PATH "zcsr engine repo")
set(ZCSR_UIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../UIM" CACHE PATH "UIM repo")

option(ZCSR_RUNTIME_BUILD_PLAYER "Build the standalone runtime player package" ON)
option(ZCSR_RUNTIME_WITH_SETTINGS_PAGE "Build optional UIM Page Manager settings page" OFF)

enable_testing()
if(NOT TARGET zcsr_glrender)
  add_subdirectory(${ZCSR_ENGINE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/zcsr_engine")
endif()
if(ZCSR_RUNTIME_BUILD_PLAYER OR ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
  if(NOT TARGET zcsr_uim)
    # Page manager is only built when this runtime explicitly opts into the
    # settings page. Runtime's default configuration has no page manager source
    # in its build graph at all unless a parent build requested it.
    if(NOT DEFINED ZCSR_UIM_WITH_PAGE_MANAGER)
      set(ZCSR_UIM_WITH_PAGE_MANAGER ${ZCSR_RUNTIME_WITH_SETTINGS_PAGE}
          CACHE BOOL "Build the optional UIM page manager target" FORCE)
    endif()
    add_subdirectory(${ZCSR_UIM_DIR} "${CMAKE_CURRENT_BINARY_DIR}/zcsr_uim")
  endif()
endif()

add_subdirectory(zcsr_task_scheduler)
add_subdirectory(zcsr_frame_loop)
add_subdirectory(zcsr_runtime_shell)

add_executable(zcsr_runtime_core_test tests/test_frame_loop.c)
target_link_libraries(zcsr_runtime_core_test PRIVATE zcsr_frame_loop zcsr_task_scheduler)
add_test(NAME zcsr_runtime_core_test COMMAND zcsr_runtime_core_test)

add_executable(zcsr_runtime_shell_autoquit_test tests/test_runtime_shell_autoquit.c)
target_link_libraries(zcsr_runtime_shell_autoquit_test PRIVATE zcsr_runtime_shell)
add_test(NAME zcsr_runtime_shell_autoquit COMMAND zcsr_runtime_shell_autoquit_test)
set_tests_properties(zcsr_runtime_shell_autoquit PROPERTIES TIMEOUT 30)

if(ZCSR_RUNTIME_BUILD_PLAYER)
  add_subdirectory(player)
endif()
