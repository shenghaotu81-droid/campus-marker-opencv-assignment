# 临时保留已存在入口装配；实现与测试已归模块，应用／工具批次将移除此文件。
if(MARK_BUILD_G2)
    add_executable(pose_app tools/apps/pose/pose_main.cpp)
    target_compile_definitions(pose_app PRIVATE VISION_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(pose_app PRIVATE mark_pose mark_detector_internal_headers)

    add_executable(pose_sensitivity tools/validation/pose_sensitivity.cpp)
    target_link_libraries(pose_sensitivity PRIVATE mark_pose vision_pose_internal_headers)
endif()
