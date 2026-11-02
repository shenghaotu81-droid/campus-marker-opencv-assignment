if(NOT VISION_BUILD_TOOLS AND NOT BUILD_TESTING)
    return()
endif()
# A02–A12：读取/执行、渲染、显示、导出、覆盖判定和报告各步骤独立声明。
add_library(vision_app offline/offline_runner.cpp completion/input_completion.cpp rendering/debug_renderer.cpp
    display/display_session.cpp export/video_exporter.cpp reporting/run_console.cpp)
add_library(vision::app ALIAS vision_app)
add_library(mark_offline ALIAS vision_app)
target_link_libraries(vision_app PRIVATE vision_detection_internal_headers)
target_include_directories(vision_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(vision_app_internal_headers INTERFACE)
target_include_directories(vision_app_internal_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vision_app_internal_headers INTERFACE vision::app vision_detection_internal_headers)
