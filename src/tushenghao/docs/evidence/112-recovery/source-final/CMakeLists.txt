cmake_minimum_required(VERSION 3.16)
project(mark_detector LANGUAGES CXX)
find_package(OpenCV REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 六个内部源码模块仍组成一个库；公开头与内部头的搜索路径分开。
add_library(mark_detector
    lib/core/timing.cpp
    lib/pipeline/diagnostics_context.cpp
    lib/pipeline/diagnostics_recorder.cpp
    lib/pipeline/run_report.cpp
    lib/core/marker_geometry.cpp
    lib/core/geometry_utils.cpp
    lib/config/config.cpp
    lib/preprocess/preprocess.cpp
    lib/geometry/geometry_matcher.cpp
    lib/geometry/geometry_observation.cpp
    lib/geometry/geometry_l_topology.cpp
    lib/geometry/geometry_anchor_evidence.cpp
    lib/geometry/geometry_validation.cpp
    lib/geometry/geometry_assignment_completion.cpp
    lib/corners/corner_resolver.cpp
    lib/corners/corner_observation.cpp
    lib/corners/corner_edge_fit.cpp
    # 单线缺失奇偶项；纯估计器由拟合层与证据复算层共用，避免沙盒全局状态。
    lib/corners/periodic_line_fit.cpp
    lib/corners/corner_evidence_validation.cpp
    lib/corners/screen_order.cpp
    lib/corners/semantic_resolver.cpp
    lib/corners/detection_validator.cpp
    lib/corners/detection_publication.cpp
    lib/pipeline/decode_stage.cpp
    lib/pipeline/frame_sequence.cpp
    lib/pipeline/temporal_geometry.cpp
    lib/pipeline/temporal_stabilizer.cpp
    lib/pipeline/display_history.cpp
    lib/pipeline/stabilize_stage.cpp
    lib/pipeline/detector.cpp
)
target_include_directories(mark_detector
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib"
)
target_link_libraries(mark_detector PUBLIC ${OpenCV_LIBS})

# 仅工程内app/测试/审计显式选择内部头，不将lib路径发布给普通Detector消费者。
add_library(mark_detector_internal_headers INTERFACE)
target_include_directories(mark_detector_internal_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/lib")
target_link_libraries(mark_detector_internal_headers INTERFACE mark_detector)
add_library(mark_test_support INTERFACE)
target_include_directories(mark_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/test_support")

add_library(mark_offline app/offline_runner.cpp app/debug_renderer.cpp app/input_completion.cpp app/display_session.cpp app/video_exporter.cpp app/run_console.cpp)
target_include_directories(mark_offline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/app")
target_link_libraries(mark_offline PUBLIC mark_detector_internal_headers)
add_executable(marker_app app/main.cpp)
target_link_libraries(marker_app PRIVATE mark_offline)

enable_testing()
# 保持原16个CTest程序；新增Block3主动检查在Release也会报告失败。
set(MARK_TEST_NAMES
    diagnostics_record_test
    debug_renderer_test
    timing_test
    observability_integration_test
    temporal_stabilizer_test
    display_history_test
    temporal_integration_test
    block3_contract_regression_test
    assignment_completion_test
    corner_edge_fit_test
    config_contract_test
    config_error_test
    config_roundtrip_test
    detector_contract_test
    geometry_model_test
    geometry_utils_test
    geometry_matcher_test
    manual_validation_check
    corner_resolver_test
    screen_order_test
    semantic_resolver_test
    detection_validator_test
    geometry_l_topology_test
    geometry_anchor_evidence_test
    path_a_competition_test
)
foreach(MARK_TEST_NAME IN LISTS MARK_TEST_NAMES)
    add_executable(${MARK_TEST_NAME} "tests/${MARK_TEST_NAME}.cpp")
    target_link_libraries(${MARK_TEST_NAME} PRIVATE mark_detector_internal_headers mark_test_support)
    add_test(NAME ${MARK_TEST_NAME} COMMAND ${MARK_TEST_NAME})
endforeach()

# 计数 helper 仅给此工具口径测试使用，不向 Detector 或公共使用者传播。
target_include_directories(corner_edge_fit_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/block3_fixture/common")
# 索引不得改变任何候选/输出；本轮完整枚举 reference 只进入回归测试，不加入生产库。
target_sources(corner_edge_fit_test PRIVATE tests/support/recovery112_reference.cpp)

# 摘要/OpenSSL只属于审计可执行程序，Detector库不引入JSON/OpenSSL。
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
add_executable(decode_audit tools/audit/decode_audit.cpp)
target_include_directories(decode_audit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(decode_audit PRIVATE mark_offline OpenSSL::Crypto)
# 旧configure偷偷调用Git；改由调用方提供标签，真实工作树另以源hash标识。
set(MARK_COMMIT_LABEL "UNSPECIFIED" CACHE STRING "外部提供基线标签，不表示工作树精确提交")
set(MARK_COMMIT "${MARK_COMMIT_LABEL}")
add_executable(temporal_audit tools/audit/temporal_audit.cpp)
target_include_directories(temporal_audit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(temporal_audit PRIVATE mark_offline OpenSSL::Crypto)
# 覆盖搬迁后的所有生产源/头及审计实现，不再只扫描根目录。
file(GLOB_RECURSE MARK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/validation/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/common/*.hpp"
)
list(APPEND MARK_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit/decode_audit.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/common/file_digest.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/common/temporal_record.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit/temporal_audit.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
)
list(REMOVE_DUPLICATES MARK_SOURCES)
list(SORT MARK_SOURCES)
set(MARK_DIGEST "")
foreach(MARK_FILE IN LISTS MARK_SOURCES)
    file(SHA256 "${MARK_FILE}" MARK_FILE_HASH)
    string(APPEND MARK_DIGEST "${MARK_FILE_HASH}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MARK_SOURCES})
string(SHA256 MARK_CODE_HASH "${MARK_DIGEST}")
target_compile_definitions(decode_audit PRIVATE MARK_DEFAULT_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/detector.yaml" MARK_COMMIT="${MARK_COMMIT}" MARK_CODE_HASH="${MARK_CODE_HASH}")

target_compile_definitions(temporal_audit PRIVATE MARK_COMMIT="${MARK_COMMIT}" MARK_CODE_HASH="${MARK_CODE_HASH}")

add_executable(geometry_audit tools/audit/geometry_audit.cpp)
target_link_libraries(geometry_audit PRIVATE mark_offline)
target_compile_definitions(mark_offline PRIVATE MARK_COMMIT="${MARK_COMMIT}" MARK_CODE_HASH="${MARK_CODE_HASH}" MARK_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
add_library(mark_frame_record_reader tools/common/frame_record_reader.cpp)
target_include_directories(mark_frame_record_reader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(mark_frame_record_reader PRIVATE mark_detector)
add_executable(observability_verify tools/validation/observability_verify.cpp)
target_include_directories(observability_verify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(observability_verify PRIVATE mark_frame_record_reader mark_detector_internal_headers OpenSSL::Crypto)

target_include_directories(diagnostics_record_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(diagnostics_record_test PRIVATE mark_frame_record_reader)
target_link_libraries(debug_renderer_test PRIVATE mark_offline)

target_include_directories(observability_integration_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(observability_integration_test PRIVATE mark_offline mark_frame_record_reader)

# Qt 探测由独立 App 进程承担；所有使用离线 runner 的目标都同时构建 sibling probe。
add_executable(marker_display_probe app/display_probe.cpp)
target_link_libraries(marker_display_probe PRIVATE ${OpenCV_LIBS})
foreach(MARK_APP_TARGET marker_app geometry_audit decode_audit temporal_audit observability_integration_test)
    add_dependencies(${MARK_APP_TARGET} marker_display_probe)
endforeach()
add_executable(final_fixes_verify tools/validation/final_fixes_verify.cpp)
target_include_directories(final_fixes_verify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(final_fixes_verify PRIVATE mark_frame_record_reader mark_detector_internal_headers)
add_executable(path_a_verify tools/validation/path_a_verify.cpp)
target_include_directories(path_a_verify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(path_a_verify PRIVATE mark_frame_record_reader mark_detector_internal_headers)

# 旧复现依赖 /tmp 和实验开关；本轮验证程序固定单线程，生成全帧结果与独立已知真值。
add_executable(recovery112_benchmark tools/validation/recovery112_benchmark.cpp)
target_link_libraries(recovery112_benchmark PRIVATE mark_detector_internal_headers)
add_executable(recovery112_synthetic tools/validation/recovery112_synthetic.cpp)
target_link_libraries(recovery112_synthetic PRIVATE mark_detector_internal_headers mark_test_support)
