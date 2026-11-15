cmake_minimum_required(VERSION 3.16)
project(zcsr_runtime LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8)
endif()

# Runtime is an engine-family library, not part of zcsr core.
# Point these at the real repos; defaults use the engine repo root.
set(ZCSR_ENGINE_DIR "C:/Users/chen/Documents/GitHub/zero-coupling-signal-router" CACHE PATH "zcsr engine repo")
set(ZCSR_UIM_DIR "C:/Users/chen/Documents/GitHub/UIM" CACHE PATH "UIM repo")

enable_testing()
add_subdirectory(${ZCSR_ENGINE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/zcsr_engine")
add_subdirectory(${ZCSR_UIM_DIR} "${CMAKE_CURRENT_BINARY_DIR}/zcsr_uim")

add_subdirectory(zcsr_runtime_shell)
