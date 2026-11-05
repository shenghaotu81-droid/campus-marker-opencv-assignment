# 独立显示探针也是 app 测试夹具，关闭普通工具不取消真实显示失败覆盖。
add_executable(marker_display_probe display/display_probe.cpp)
target_link_libraries(marker_display_probe PRIVATE ${OpenCV_LIBS})
if(VISION_BUILD_TOOLS)
    add_executable(marker_app marker/main.cpp)
    target_link_libraries(marker_app PRIVATE vision_app_internal_headers)
    target_compile_definitions(marker_app PRIVATE VISION_SOURCE_ROOT="${VISION_ROOT}")
    add_dependencies(marker_app marker_display_probe)
endif()
