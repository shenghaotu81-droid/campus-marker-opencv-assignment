# 当前帧：分割、匹配、原图角拟合与检测结果发布。
add_library(vision_detection
    preprocess/preprocess.cpp
    geometry/match_quad.cpp
    geometry/geometry_observation.cpp
    corners/corner_observation.cpp
    corners/corner_edge_fit.cpp
    corners/screen_order.cpp
    pipeline/decode_stage.cpp
    pipeline/detector.cpp)
target_link_libraries(vision_detection PUBLIC vision_types vision_config)
target_include_directories(vision_detection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
