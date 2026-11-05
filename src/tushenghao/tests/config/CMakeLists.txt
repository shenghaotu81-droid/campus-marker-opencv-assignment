# Q02/Q03/Q06：配置负例与往返独立装配，原用例与期望不变。
if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    foreach(test_name config_error_test config_roundtrip_test)
        add_executable(${test_name} ${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE vision_detection mark_test_support vision_calibration)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

get_property(test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS test_targets)
    target_include_directories(${target} PRIVATE "${VISION_ROOT}/src/config" "${VISION_ROOT}/src/detection")
endforeach()
