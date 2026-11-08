if(NOT VISION_BUILD_TOOLS AND NOT BUILD_TESTING)
    return()
endif()
# 应用串联检测、位姿、跟踪和输出，模块算法之间不经应用层反向调用。
add_library(vision_app offline/offline_runner.cpp rendering/debug_renderer.cpp
    export/video_exporter.cpp reporting/run_console.cpp reporting/run_report.cpp)
target_include_directories(vision_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${VISION_ROOT}/src/tracking")
target_link_libraries(vision_app PRIVATE vision_detection vision_pose vision_tracking)
