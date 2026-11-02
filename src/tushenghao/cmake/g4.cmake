# 实现／测试已归模块；入口装配在工具批次迁移。
if(MARK_BUILD_G4)
    add_executable(tracking_app app/tracking_main.cpp)
    target_link_libraries(tracking_app PRIVATE vision_tracking_internal_headers)

    add_executable(tracking_lab tools/validation/tracking_lab.cpp)
    target_link_libraries(tracking_lab PRIVATE vision_tracking_internal_headers)

endif()
