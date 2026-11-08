# 跟踪消费位姿量测，数值安全检查与滤波器放在同一模块。
add_library(vision_tracking filter/kalman_cv.cpp filter/covariance_validation.cpp
    measurement/measurement_adapter.cpp lifecycle/target_tracker.cpp io/tracking_io.cpp)
target_include_directories(vision_tracking PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vision_tracking PUBLIC vision_pose vision_types vision_config)
target_compile_options(vision_tracking PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)
