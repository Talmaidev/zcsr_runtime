cmake_minimum_required(VERSION 3.16)
include(GNUInstallDirs)
add_library(zcsr_frame_loop STATIC
  src/frame_loop.c
)
target_include_directories(zcsr_frame_loop PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(zcsr_frame_loop PUBLIC c_std_11)
