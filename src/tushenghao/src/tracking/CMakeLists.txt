# G4独立上层库；不把跟踪状态塞进Detector或冻结G2类型。
option(MARK_BUILD_G4 "构建G4帧间跟踪与速度估计" OFF)
if(MARK_BUILD_G4)
    if(NOT MARK_BUILD_G2)
        message(FATAL_ERROR "G4使用已冻结G2类型，请同时指定-DMARK_BUILD_G2=ON")
    endif()
    add_library(vision_tracking

        filter/kalman_cv.cpp
        measurement/measurement_adapter.cpp
        lifecycle/target_tracker.cpp
        io/tracking_io.cpp
    )
    target_include_directories(vision_tracking PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(vision_tracking PUBLIC vision::pose vision::types vision::config PRIVATE vision::math)
    target_compile_options(vision_tracking PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)

    add_library(vision::tracking ALIAS vision_tracking)
    add_library(mark_tracking ALIAS vision_tracking)
    add_library(vision_tracking_internal_headers INTERFACE)
    target_include_directories(vision_tracking_internal_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(vision_tracking_internal_headers INTERFACE vision::tracking)
endif()
