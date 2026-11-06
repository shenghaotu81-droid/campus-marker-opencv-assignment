add_executable(config_error_test config_error_test.cpp)
target_link_libraries(config_error_test PRIVATE vision_detection mark_test_support)
target_include_directories(config_error_test PRIVATE "${VISION_ROOT}/src/config")
add_test(NAME config_error_test COMMAND config_error_test)
