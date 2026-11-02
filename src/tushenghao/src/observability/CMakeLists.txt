# A06/A07/A12：只读值、计时、编码、运行生命周期分开；禁止链接检测实现。
add_library(vision_observability timing/timing.cpp events/frame_events.cpp context/diagnostics_context.cpp
    recording/record_codec.cpp recording/diagnostics_recorder.cpp reporting/run_report.cpp identity/file_fingerprint.cpp)
add_library(vision::observability ALIAS vision_observability)
target_link_libraries(vision_observability PUBLIC vision::types PRIVATE vision_config_internal_headers)
target_include_directories(vision_observability PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(vision_observability_internal_headers INTERFACE)
target_include_directories(vision_observability_internal_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vision_observability_internal_headers INTERFACE vision::observability vision_config_internal_headers)
