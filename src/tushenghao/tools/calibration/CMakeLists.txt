# 标定实现库由 calibration_tool 链接。
add_library(vision_calibration input/calibration_config.cpp frames/calibration_video.cpp
    observation/calibration_detection.cpp solution/calibration_result.cpp)
target_include_directories(vision_calibration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vision_calibration PUBLIC ${OpenCV_LIBS})
target_compile_definitions(vision_calibration PRIVATE VISION_SOURCE_ROOT="${VISION_ROOT}")
if(VISION_BUILD_TOOLS)
    add_executable(calibration_tool apps/calibration_main.cpp)
    target_link_libraries(calibration_tool PRIVATE vision_calibration)
endif()
