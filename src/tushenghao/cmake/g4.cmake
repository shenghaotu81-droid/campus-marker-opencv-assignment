# 实现／测试已归模块；入口装配在工具批次迁移。
if(MARK_BUILD_G4)
    add_executable(tracking_app tools/apps/tracking/tracking_main.cpp)
    target_compile_definitions(tracking_app PRIVATE VISION_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tracking_app PRIVATE vision_tracking_internal_headers)

    add_executable(tracking_lab tools/validation/tracking_lab.cpp)
    target_link_libraries(tracking_lab PRIVATE vision_tracking_internal_headers)

endif()
