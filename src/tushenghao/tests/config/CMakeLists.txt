# Q02/Q03/Q06：配置负例与往返独立装配，原用例与期望不变。
if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    foreach(test_name config_error_test config_roundtrip_test)
        add_executable(${test_name} ${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE mark_detector_internal_headers mark_test_support vision_calibration)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
