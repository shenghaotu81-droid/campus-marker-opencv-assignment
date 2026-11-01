# G4独立上层库；不把跟踪状态塞进Detector或冻结G2类型。
option(MARK_BUILD_G4 "构建G4帧间跟踪与速度估计" OFF)
if(MARK_BUILD_G4)
    if(NOT MARK_BUILD_G2)
        message(FATAL_ERROR "G4使用已冻结G2类型，请同时指定-DMARK_BUILD_G2=ON")
    endif()
    add_library(mark_tracking
        lib/config/tracking_config.cpp
        lib/tracking/kalman_cv.cpp
        lib/tracking/measurement_adapter.cpp
        lib/tracking/target_tracker.cpp
        lib/tracking/tracking_io.cpp
    )
    target_include_directories(mark_tracking PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    target_link_libraries(mark_tracking PUBLIC mark_pose)
    target_compile_options(mark_tracking PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)

    add_executable(tracking_app app/tracking_main.cpp)
    target_include_directories(tracking_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    target_link_libraries(tracking_app PRIVATE mark_tracking)

    add_executable(tracking_lab tools/validation/tracking_lab.cpp)
    target_include_directories(tracking_lab PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    target_link_libraries(tracking_lab PRIVATE mark_tracking)

    foreach(test_name tracking_contract_test tracking_io_test)
        add_executable(${test_name} "tests/${test_name}.cpp")
        target_include_directories(${test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib")
        target_link_libraries(${test_name} PRIVATE mark_tracking)
        target_compile_definitions(${test_name} PRIVATE VISION_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
