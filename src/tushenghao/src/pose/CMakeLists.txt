if(MARK_BUILD_G2)
    if(OpenCV_VERSION VERSION_LESS "4.6.0")
        message(FATAL_ERROR "G2 要求 OpenCV >= 4.6.0；已验证版本为 4.6.0")
    endif()
    # Ubuntu 24.04 的 3.3.0 包有 CMake 多架构前缀问题；用同包提供的 pkg-config。
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(APRILTAG REQUIRED apriltag>=3.3)
    find_package(Threads REQUIRED)
    find_path(APRILTAG_INCLUDE_ROOT NAMES apriltag/apriltag.h HINTS ${APRILTAG_INCLUDE_DIRS})
    find_library(APRILTAG_LIBRARY NAMES apriltag HINTS ${APRILTAG_LIBRARY_DIRS})
    if(NOT APRILTAG_INCLUDE_ROOT OR NOT APRILTAG_LIBRARY)
        message(FATAL_ERROR "找不到 AprilTag 3 头文件或库；安装 libapriltag-dev 或设置 CMAKE_PREFIX_PATH")
    endif()
    add_library(mark_apriltag INTERFACE)
    target_include_directories(mark_apriltag SYSTEM INTERFACE "${APRILTAG_INCLUDE_ROOT}")
    target_link_libraries(mark_apriltag INTERFACE "${APRILTAG_LIBRARY}" Threads::Threads)
    add_library(vision_pose
        io/pose_io.cpp
        validation/pose_geometry.cpp
        estimation/pose_estimator.cpp
        tag/tag_detector.cpp
    )
    target_include_directories(vision_pose
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${VISION_ROOT}/src/config"
    )
    target_link_libraries(vision_pose PUBLIC vision_types vision_config PRIVATE mark_apriltag)
    target_compile_options(vision_pose PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )

endif()
