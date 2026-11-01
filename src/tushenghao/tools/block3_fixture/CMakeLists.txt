# 独立fixture工具只链接旧核心库；Detector不引入生成器JSON/OpenSSL依赖。
cmake_minimum_required(VERSION 3.16)
project(block3_fixture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SYNTH_REF_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../synth_ref" CACHE PATH "已有synthetic_gen核心源码")
if(NOT TARGET synthetic_gen_core)
    add_subdirectory("${SYNTH_REF_SOURCE}" synthetic_ref EXCLUDE_FROM_ALL)
endif()
add_executable(block3_fixture generation/main.cpp)
target_link_libraries(block3_fixture PRIVATE synthetic_gen_core)
target_compile_definitions(block3_fixture PRIVATE MODEL_YAML="${CMAKE_CURRENT_SOURCE_DIR}/../../config/marker_geometry.yaml")

# 用途分目录，CMake入口和可执行目标名保持兼容。
# 隔离定位runner仅工具目标连接Detector与真值读取；正式Detector库无反向依赖。
# B02：父工程已有目标时只链接；独立入口仍需旧根装配，模块 CMake 拆分留搬家。
if(NOT TARGET mark_detector_internal_headers)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." detector EXCLUDE_FROM_ALL)
endif()
add_executable(block3_measure calibration/measure.cpp)
target_link_libraries(block3_measure PRIVATE mark_detector_internal_headers synthetic_gen_core)
target_compile_definitions(block3_measure PRIVATE DETECTOR_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../../config/detector.yaml")

file(GLOB_RECURSE MEASURE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../app/*.cpp"
)
file(GLOB_RECURSE MEASURE_TOOL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(APPEND MEASURE_SOURCES ${MEASURE_TOOL_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/common/support_pixel_count.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/common/file_digest.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../test_support/block3_fixture.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
)
list(SORT MEASURE_SOURCES)
set(MEASURE_DIGEST "")
foreach(MEASURE_FILE IN LISTS MEASURE_SOURCES)
    file(SHA256 "${MEASURE_FILE}" MEASURE_FILE_HASH)
    string(APPEND MEASURE_DIGEST "${MEASURE_FILE_HASH}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MEASURE_SOURCES})
string(SHA256 MEASURE_CODE_HASH "${MEASURE_DIGEST}")
target_compile_definitions(block3_measure PRIVATE MEASURE_CODE_HASH="${MEASURE_CODE_HASH}")

add_executable(block3_resources validation/resources.cpp)
target_link_libraries(block3_resources PRIVATE mark_detector_internal_headers mark_test_support)
add_executable(block3_verify validation/verify.cpp)
target_link_libraries(block3_verify PRIVATE mark_detector_internal_headers synthetic_gen_core)
target_compile_definitions(block3_verify PRIVATE MEASURE_CODE_HASH="${MEASURE_CODE_HASH}" DETECTOR_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../../config/detector.yaml")

# 实拍拒绝原因只读诊断；仍复用Detector，不改变生产预算或读取真值。
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
add_executable(block3_video_diagnose diagnostics/video_diagnose.cpp)
target_link_libraries(block3_video_diagnose PRIVATE mark_detector_internal_headers nlohmann_json::nlohmann_json)
add_executable(block3_video_corner_measure diagnostics/video_corner_measure.cpp)
target_link_libraries(block3_video_corner_measure PRIVATE mark_detector_internal_headers nlohmann_json::nlohmann_json)
add_executable(block3_parent_diagnose diagnostics/parent_diagnose.cpp)
target_include_directories(block3_parent_diagnose PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(block3_parent_diagnose PRIVATE mark_detector_internal_headers nlohmann_json::nlohmann_json OpenSSL::Crypto)
