
add_executable(logger_example logger_example.cpp)
target_link_libraries(logger_example absl::log absl::log_initialize absl_check absl::flags)