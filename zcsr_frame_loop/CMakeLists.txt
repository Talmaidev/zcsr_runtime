add_library(zcsr_frame_loop STATIC
  src/frame_loop.c
)
target_include_directories(zcsr_frame_loop PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(zcsr_frame_loop PUBLIC c_std_11)
