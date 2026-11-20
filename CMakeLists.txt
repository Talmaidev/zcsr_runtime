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

if(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
  # Shell-level settings keyboard routing: S/R/Esc/Tab/arrows are queued into
  # the shell's zcsr_input ring, not passed directly to the page handler.
  add_executable(zcsr_runtime_shell_settings_keys_test tests/test_runtime_shell_settings_keys.c)
  target_compile_definitions(zcsr_runtime_shell_settings_keys_test PRIVATE
      ZCSR_RUNTIME_WITH_SETTINGS_PAGE=1)
  target_link_libraries(zcsr_runtime_shell_settings_keys_test PRIVATE zcsr_runtime_shell)
  add_test(NAME zcsr_runtime_shell_settings_keys COMMAND zcsr_runtime_shell_settings_keys_test)
  set_tests_properties(zcsr_runtime_shell_settings_keys PROPERTIES TIMEOUT 30)
endif()

# The 2.5D integration slice used to be source-only in the engine repository.
# Build it here through the canonical runtime host so it is compiled and run,
# not just text-checked. It uses the same zcsr_window_game_run window/loop.
if(EXISTS "${ZCSR_ENGINE_DIR}/integration/testmain.c")
  add_executable(zcsr_light2d_integration_slice
      "${ZCSR_ENGINE_DIR}/integration/testmain.c")
  target_include_directories(zcsr_light2d_integration_slice PRIVATE
      ${ZCSR_ENGINE_DIR}/include
      ${ZCSR_ENGINE_DIR}/third_party/glad/include)
  target_link_libraries(zcsr_light2d_integration_slice PRIVATE
      zcsr_runtime_shell zcsr_light2d zcsr_sprite zcsr_camera zcsr_glrender)
  add_test(NAME zcsr_light2d_integration_slice
           COMMAND zcsr_light2d_integration_slice autoquit)
  set_tests_properties(zcsr_light2d_integration_slice PROPERTIES
      TIMEOUT 30
      PASS_REGULAR_EXPRESSION "all PASS")
endif()

if(ZCSR_RUNTIME_BUILD_PLAYER)
  add_subdirectory(player)
endif()
