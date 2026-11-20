cmake_minimum_required(VERSION 3.16)
project(zcsr_player LANGUAGES C)

# Standalone game runtime. It consumes UIM project/page rendering and the
# native runtime host; it never compiles editor chrome, ImGui, or EditorKernel.
add_executable(zcsr_player
    player_main.c
    player_module.c)

target_include_directories(zcsr_player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ZCSR_ENGINE_DIR}/include
    ${ZCSR_ENGINE_DIR}/third_party/glad/include
    ${ZCSR_UIM_DIR}/include
    ${ZCSR_UIM_DIR}/integrations/zcsr
    ${CMAKE_CURRENT_SOURCE_DIR}/../zcsr_runtime_shell/include)

if(MSVC)
  target_compile_options(zcsr_player PRIVATE /experimental:c11atomics)
endif()

target_link_libraries(zcsr_player PRIVATE
    zcsr_runtime_shell
    zcsr_uim_project
    zcsr_uim_page_view
    zcsr_uim
    zcsr_uim_zcsr_bridge
    zcsr_ui
    zcsr_glrender
    zcsr_platform
    zcsr_core
    zcsr_font
    zcsr_image
    zcsr_input
    zcsr_inputext
    zcsr_camera
    zcsr_camera3d
    zcsr_sprite
    zcsr_assets
    zcsr_viewport)

if(WIN32)
  target_link_libraries(zcsr_player PRIVATE user32 gdi32 opengl32)
endif()

# Keep the packaged player next to the editor executable in a shared build.
set_target_properties(zcsr_player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel")

add_test(NAME zcsr_player_autoquit
         COMMAND zcsr_player --play
                 "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/rule_demo.uireproj"
                 autoquit)
set_tests_properties(zcsr_player_autoquit PROPERTIES
    TIMEOUT 30
    PASS_REGULAR_EXPRESSION "window shell ended")
