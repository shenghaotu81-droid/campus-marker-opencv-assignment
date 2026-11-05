# 当前帧：观察、match四边形、原图拟合、排序、校验与RAW发布。
add_library(vision_detection
    preprocess/preprocess.cpp
    geometry/matching/match_quad.cpp
    geometry/observation/geometry_observation.cpp
    corners/measurement/corner_resolver.cpp
    corners/observation/corner_observation.cpp
    corners/fitting/corner_edge_fit.cpp
    corners/fitting/periodic_line_fit.cpp
    corners/validation/corner_evidence_validation.cpp
    corners/ordering/screen_order.cpp
    corners/validation/detection_validator.cpp
    corners/publication/detection_publication.cpp
    pipeline/decode/decode_stage.cpp
    pipeline/input/frame_sequence.cpp
    pipeline/execution/detector.cpp
)
target_link_libraries(vision_detection PUBLIC vision_types vision_config
    PRIVATE vision_math)
target_include_directories(vision_detection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# 工程内测试／探针显式选择步骤声明，不向普通 Detector 消费者暴露实现树。

target_include_directories(vision_detection PRIVATE "${VISION_ROOT}/src/config")
