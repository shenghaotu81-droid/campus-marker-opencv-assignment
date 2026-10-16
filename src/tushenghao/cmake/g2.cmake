# 独立开关和编译目标
# G2 独立编译目标：不把位姿/退出逻辑塞入已封口的 mark_detector 源码清单。
# G2 的独立构建开关（MARK_BUILD_G2 默认 OFF），主 CMakeLists.txt 末尾加一行 include(cmake/g2.cmake) 
# 定义 G2 的编译目标（mark_pose 库、pose_app 等），找 AprilTag 依赖
# MARK_BUILD_G2 默认 OFF，不影响原检测器构建；开 G2 时 cmake -DMARK_BUILD_G2=ON

# option() 定义开关，名字、说明、默认值 OFF
option(MARK_BUILD_G2 "构建 G2 位姿与退出感知" OFF)
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
    add_library(mark_pose
        lib/config/pose_config.cpp
        lib/pose/pose_geometry.cpp
        lib/pose/pose_estimator.cpp
        lib/pose/tag_detector.cpp
    )
    target_include_directories(mark_pose
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    )
    target_link_libraries(mark_pose PUBLIC mark_detector PRIVATE mark_apriltag)
    target_compile_options(mark_pose PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )

    add_executable(pose_app app/pose_main.cpp)
    target_link_libraries(pose_app PRIVATE mark_pose mark_detector_internal_headers)

    add_executable(pose_contract_test tests/pose_contract_test.cpp)
    target_include_directories(pose_contract_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    target_link_libraries(pose_contract_test PRIVATE mark_pose)
    add_test(NAME pose_contract_test COMMAND pose_contract_test)

    add_executable(tag_detector_test tests/tag_detector_test.cpp)
    target_link_libraries(tag_detector_test PRIVATE mark_pose mark_apriltag)
    # Debian/Ubuntu 将图像生成辅助符号拆进 utils；只供测试 fixture，不加入生产 API。
    find_library(APRILTAG_UTILS NAMES apriltag-utils HINTS ${APRILTAG_LIBRARY_DIRS})
    if(APRILTAG_UTILS)
        target_link_libraries(tag_detector_test PRIVATE ${APRILTAG_UTILS})
    endif()
    add_test(NAME tag_detector_test COMMAND tag_detector_test)

    add_executable(pose_sensitivity tools/validation/pose_sensitivity.cpp)
    target_link_libraries(pose_sensitivity PRIVATE mark_pose)
endif()