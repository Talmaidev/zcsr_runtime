find_package(Threads REQUIRED)
add_library(zcsr_task_scheduler STATIC
  src/task_scheduler.c
)
target_include_directories(zcsr_task_scheduler PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(zcsr_task_scheduler PUBLIC Threads::Threads)
target_compile_features(zcsr_task_scheduler PUBLIC c_std_11)
