# 当前帧：分割、匹配、原图角拟合与检测结果发布。
add_library(vision_detection preprocess.cpp match_quad.cpp geometry_observation.cpp
    corner_observation.cpp corner_edge_fit.cpp screen_order.cpp decode_stage.cpp detector.cpp)
target_link_libraries(vision_detection PUBLIC vision_types vision_config)
target_include_directories(vision_detection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
