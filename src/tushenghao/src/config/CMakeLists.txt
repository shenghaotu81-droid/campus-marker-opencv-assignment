# 配置库只读取参数，不依赖检测、位姿或跟踪算法。
add_library(vision_config detector/detector_config.cpp application/offline_config.cpp
    pose/pose_config.cpp tracking/tracking_config.cpp)
target_link_libraries(vision_config PUBLIC vision_types)
target_include_directories(vision_config PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
