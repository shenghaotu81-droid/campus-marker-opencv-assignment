# D/T：各子目录对应观察、父假设、补全、取证、排序、校验、发布与时序步骤。
add_library(vision_detection
    preprocess/preprocess.cpp
    geometry/parent/geometry_matcher.cpp
    geometry/observation/geometry_observation.cpp
    geometry/observation/geometry_l_topology.cpp
    geometry/parent/geometry_anchor_evidence.cpp
    geometry/validation/geometry_validation.cpp
    geometry/completion/geometry_assignment_completion.cpp
    corners/measurement/corner_resolver.cpp
    corners/observation/corner_observation.cpp
    corners/fitting/corner_edge_fit.cpp
    corners/fitting/periodic_line_fit.cpp
    corners/validation/corner_evidence_validation.cpp
    corners/ordering/screen_order.cpp
    corners/semantics/semantic_resolver.cpp
    corners/validation/detection_validator.cpp
    corners/publication/detection_publication.cpp
    pipeline/decode/decode_stage.cpp
    pipeline/input/frame_sequence.cpp
    temporal/correspondence/temporal_geometry.cpp
    temporal/stabilization/temporal_stabilizer.cpp
    temporal/display/display_history.cpp
    temporal/assembly/stabilize_stage.cpp
    pipeline/execution/detector.cpp
)
add_library(vision::detection ALIAS vision_detection)
add_library(mark_detector ALIAS vision_detection)
target_link_libraries(vision_detection PUBLIC vision::types vision::config
    PRIVATE vision::math vision_observability_internal_headers vision_config_internal_headers)
target_include_directories(vision_detection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# 工程内测试／探针显式选择步骤声明，不向普通 Detector 消费者暴露实现树。
add_library(vision_detection_internal_headers INTERFACE)
add_library(mark_detector_internal_headers ALIAS vision_detection_internal_headers)
target_include_directories(vision_detection_internal_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vision_detection_internal_headers INTERFACE vision::detection
    vision_observability_internal_headers vision_config_internal_headers)
