cmake_minimum_required(VERSION 3.16)

# --- Hard guard: the build tree must live OUTSIDE the source tree ------------
# Same layout guard as zero-coupling-signal-router. CMake 3.16 compatible
# (file(TO_CMAKE_PATH), no cmake_path): in-source / in-repo build directories
# are refused at configure time so the source tree stays free of build output.
if(CMAKE_BINARY_DIR)
  file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" _zcsr_guard_src)
  file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}" _zcsr_guard_bin)
  if(WIN32)
    string(TOLOWER "${_zcsr_guard_src}" _zcsr_guard_src_cmp)
    string(TOLOWER "${_zcsr_guard_bin}" _zcsr_guard_bin_cmp)
  else()
    set(_zcsr_guard_src_cmp "${_zcsr_guard_src}")
    set(_zcsr_guard_bin_cmp "${_zcsr_guard_bin}")
  endif()
  string(LENGTH "${_zcsr_guard_src_cmp}" _zcsr_guard_src_len)
  string(SUBSTRING "${_zcsr_guard_bin_cmp}" 0 ${_zcsr_guard_src_len}
         _zcsr_guard_bin_head)
  set(_zcsr_guard_in_repo FALSE)
  if(_zcsr_guard_bin_cmp STREQUAL _zcsr_guard_src_cmp)
    set(_zcsr_guard_in_repo TRUE)
  elseif(_zcsr_guard_bin_head STREQUAL _zcsr_guard_src_cmp)
    string(SUBSTRING "${_zcsr_guard_bin_cmp}" ${_zcsr_guard_src_len} 1
           _zcsr_guard_sep)
    if(_zcsr_guard_sep STREQUAL "/" OR _zcsr_guard_sep STREQUAL "\\")
      set(_zcsr_guard_in_repo TRUE)
    endif()
  endif()
  if(_zcsr_guard_in_repo)
    message(FATAL_ERROR
      "In-repo build directory refused by ZCSR build-layout guard.\n"
      "  CMAKE_SOURCE_DIR = ${CMAKE_SOURCE_DIR}\n"
      "  CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}\n"
      "\n"
      "Build trees must live outside the source tree: this repository (like\n"
      "every repository under C:\\Users\\chen\\Documents\\GitHub) must stay clean.\n"
      "Put the build tree at one of these instead:\n"
      "  Windows: C:\\Users\\chen\\Documents\\build\\zcsr\\zcsr_runtime\\<preset>\n"
      "  Linux:   /home/chen/zcsr-builds/zcsr_runtime/<preset>\n"
      "\n"
      "Example (Windows, Visual Studio generator):\n"
      "  cmake -S . -B \"C:\\Users\\chen\\Documents\\build\\zcsr\\zcsr_runtime\\build_main\" -G \"Visual Studio 18 2026\"\n"
      "Example (Linux):\n"
      "  cmake -S . -B /home/chen/zcsr-builds/zcsr_runtime/linux-gcc\n")
  endif()
  unset(_zcsr_guard_src)
  unset(_zcsr_guard_bin)
  unset(_zcsr_guard_src_cmp)
  unset(_zcsr_guard_bin_cmp)
  unset(_zcsr_guard_src_len)
  unset(_zcsr_guard_bin_head)
  unset(_zcsr_guard_sep)
  unset(_zcsr_guard_in_repo)
endif()

project(zcsr_runtime VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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
option(ZCSR_RUNTIME_WITH_SETTINGS_PAGE "Build the independent setting page manager overlay" OFF)

# --- Install / export package -------------------------------------------------

enable_testing()
if(NOT TARGET zcsr_glrender)
  add_subdirectory(${ZCSR_ENGINE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/zcsr_engine")
endif()
if(ZCSR_RUNTIME_BUILD_PLAYER OR ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
  if(NOT TARGET zcsr_uim)
    # Page manager is only built when this runtime explicitly opts into the
    # settings page. Runtime's default configuration has no page manager source
    # in its build graph at all unless a parent build requested it.
    if(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
      set(ZCSR_UIM_WITH_PAGE_MANAGER ON
          CACHE BOOL "Build the optional UIM page manager target" FORCE)
    elseif(NOT DEFINED ZCSR_UIM_WITH_PAGE_MANAGER)
      set(ZCSR_UIM_WITH_PAGE_MANAGER OFF
          CACHE BOOL "Build the optional UIM page manager target" FORCE)
    endif()
    add_subdirectory(${ZCSR_UIM_DIR} "${CMAKE_CURRENT_BINARY_DIR}/zcsr_uim")
  endif()
endif()

add_subdirectory(zcsr_task_scheduler)
add_subdirectory(zcsr_frame_loop)
add_subdirectory(zcsr_runtime_shell)

# --- Install / export package -------------------------------------------------
install(TARGETS zcsr_task_scheduler zcsr_frame_loop zcsr_runtime_shell
  EXPORT zcsr_runtimeTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
  zcsr_task_scheduler/include/zcsr_task_scheduler.h
  zcsr_frame_loop/include/zcsr_frame_loop.h
  zcsr_runtime_shell/include/zcsr_runtime_shell.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT zcsr_runtimeTargets
  FILE zcsr_runtimeTargets.cmake
  NAMESPACE zcsr::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zcsr_runtime)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zcsr_runtimeConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/zcsr_runtimeConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zcsr_runtime)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/zcsr_runtimeConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/zcsr_runtimeConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/zcsr_runtimeConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zcsr_runtime)

add_executable(zcsr_runtime_core_test tests/test_frame_loop.c)
target_link_libraries(zcsr_runtime_core_test PRIVATE zcsr_frame_loop zcsr_task_scheduler)
add_test(NAME zcsr_runtime_core_test COMMAND zcsr_runtime_core_test)
set_tests_properties(zcsr_runtime_core_test PROPERTIES
    LABELS "module;runtime;fast")

add_executable(zcsr_runtime_shell_autoquit_test tests/test_runtime_shell_autoquit.c)
target_link_libraries(zcsr_runtime_shell_autoquit_test PRIVATE zcsr_runtime_shell)
add_test(NAME zcsr_runtime_shell_autoquit COMMAND zcsr_runtime_shell_autoquit_test)
set_tests_properties(zcsr_runtime_shell_autoquit PROPERTIES TIMEOUT 30
    LABELS "module;runtime;fast")

if(ZCSR_RUNTIME_WITH_SETTINGS_PAGE)
  # Shell-level settings keyboard routing: S/R/Esc/Tab/arrows are queued into
  # the shell's zcsr_input ring, not passed directly to the page handler.
  add_executable(zcsr_runtime_shell_settings_keys_test tests/test_runtime_shell_settings_keys.c)
  target_compile_definitions(zcsr_runtime_shell_settings_keys_test PRIVATE
      ZCSR_RUNTIME_WITH_SETTINGS_PAGE=1)
  target_link_libraries(zcsr_runtime_shell_settings_keys_test PRIVATE zcsr_runtime_shell)
  add_test(NAME zcsr_runtime_shell_settings_keys COMMAND zcsr_runtime_shell_settings_keys_test)
  set_tests_properties(zcsr_runtime_shell_settings_keys PROPERTIES TIMEOUT 30
      LABELS "module;runtime;fast")
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
      LABELS "module;runtime;light2d;fast"
      RESOURCE_LOCK zcsr_gui_display)
endif()

if(ZCSR_RUNTIME_BUILD_PLAYER)
  add_subdirectory(player)
endif()
