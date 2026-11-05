if(NOT BUILD_TESTING)
    return()
endif()
add_subdirectory(config)
add_subdirectory(detection)
add_subdirectory(app)
add_subdirectory(pose)
add_subdirectory(tracking)

get_property(test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS test_targets)
    target_include_directories(${target} PRIVATE "${VISION_ROOT}/src/config" "${VISION_ROOT}/src/detection"
        "${VISION_ROOT}/src/pose" "${VISION_ROOT}/src/tracking" "${VISION_ROOT}/src/app" "${VISION_ROOT}/src/observability")
endforeach()
