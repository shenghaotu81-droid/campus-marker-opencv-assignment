cmake_minimum_required(VERSION 3.16)
project(mark_detector LANGUAGES CXX)
find_package(OpenCV REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(src/types)
add_subdirectory(src/math)
add_subdirectory(src/config)
add_subdirectory(src/observability)

# 六个内部源码模块仍组成一个库；公开头与内部头的搜索路径分开。
add_subdirectory(src/detection)
add_subdirectory(src/pose)
add_subdirectory(src/tracking)

add_library(mark_test_support INTERFACE)
target_include_directories(mark_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/test_support"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/detection"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/detection/temporal"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/detection/corners")
target_compile_definitions(mark_test_support INTERFACE VISION_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

add_library(mark_offline app/offline_runner.cpp app/debug_renderer.cpp app/input_completion.cpp app/display_session.cpp app/video_exporter.cpp app/run_console.cpp)
target_include_directories(mark_offline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/app")
target_link_libraries(mark_offline PUBLIC mark_detector_internal_headers)
add_executable(marker_app app/main.cpp)
target_link_libraries(marker_app PRIVATE mark_offline)

enable_testing()
# 活跃测试逐项注册；删除的空测试与无业务消费者 helper 专属测试不计入覆盖。
set(MARK_TEST_NAMES
    debug_renderer_test
    observability_integration_test
    manual_validation_check
)
foreach(MARK_TEST_NAME IN LISTS MARK_TEST_NAMES)
    add_executable(${MARK_TEST_NAME} "tests/${MARK_TEST_NAME}.cpp")
    target_link_libraries(${MARK_TEST_NAME} PRIVATE mark_detector_internal_headers mark_test_support)
    add_test(NAME ${MARK_TEST_NAME} COMMAND ${MARK_TEST_NAME})
endforeach()

add_subdirectory(tests/config)
add_subdirectory(tests/observability)
add_subdirectory(tests/detection)
add_subdirectory(tests/pose)
add_subdirectory(tests/tracking)

# F61：配置负例直接调用标定解析器；只为已有测试追加实现，不新增目标。
target_sources(config_error_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools/calibration/calibration_config.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/calibration/calibration_video.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/tools/calibration/calibration_detection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/calibration/calibration_result.cpp")

# 计数 helper 仅给此工具口径测试使用，不向 Detector 或公共使用者传播。
target_include_directories(corner_edge_fit_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/block3_fixture/common")
# 索引不得改变任何候选/输出；本轮完整枚举 reference 只进入回归测试，不加入生产库。
target_sources(corner_edge_fit_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/detection/corners/recovery112_reference.cpp")

# 摘要/OpenSSL只属于审计可执行程序，Detector库不引入JSON/OpenSSL。
add_executable(decode_audit tools/audit/decode_audit.cpp)
# B01：OpenSSL 只由实际使用 SHA 的审计工具请求；模块开关在搬家时划分。
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
target_include_directories(decode_audit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(decode_audit PRIVATE mark_offline OpenSSL::Crypto)
# 旧configure偷偷调用Git；改由调用方提供标签，真实工作树另以源hash标识。
set(MARK_COMMIT_LABEL "UNSPECIFIED" CACHE STRING "外部提供基线标签，不表示工作树精确提交")
set(MARK_COMMIT "${MARK_COMMIT_LABEL}")
add_executable(temporal_audit tools/audit/temporal_audit.cpp)
target_include_directories(temporal_audit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(temporal_audit PRIVATE mark_offline OpenSSL::Crypto)
target_compile_definitions(decode_audit PRIVATE MARK_DEFAULT_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/detector.yaml" MARK_COMMIT="${MARK_COMMIT}")
target_compile_definitions(temporal_audit PRIVATE MARK_COMMIT="${MARK_COMMIT}")
add_executable(geometry_audit tools/audit/geometry_audit.cpp)
target_link_libraries(geometry_audit PRIVATE mark_offline)
target_compile_definitions(mark_offline PRIVATE MARK_COMMIT="${MARK_COMMIT}" MARK_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
add_library(mark_frame_record_reader tools/common/frame_record_reader.cpp)
target_include_directories(mark_frame_record_reader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(mark_frame_record_reader PRIVATE mark_detector_internal_headers)
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

# 离线相机标定工具：7x7 圆点阵（0.03m 间距），求内参 K 和畸变 dist。
add_executable(
    calibration_tool
    tools/calibration/calibration_main.cpp
    tools/calibration/calibration_config.cpp
    tools/calibration/calibration_video.cpp
    tools/calibration/calibration_detection.cpp
    tools/calibration/calibration_result.cpp
)
target_link_libraries(calibration_tool PRIVATE ${OpenCV_LIBS})

# G2 独立感知层，默认关闭；不改变原检测器目标。
include(cmake/g2.cmake)
# G4独立感知估计层，原G2保持冻结。
include(cmake/g4.cmake)

# B05：沿实际链接图登记源文件，再展开工程内的显式 include；不扫描未链接模块。
function(mark_collect_target_inputs target output_files output_includes)
    get_target_property(imported "${target}" IMPORTED)
    if(imported)
        set(${output_files} "" PARENT_SCOPE)
        set(${output_includes} "" PARENT_SCOPE)
        return()
    endif()
    get_target_property(source_dir "${target}" SOURCE_DIR)
    set(files "${source_dir}/CMakeLists.txt")
    set(includes "")
    foreach(property SOURCES INTERFACE_SOURCES MARK_CMAKE_INPUTS MARK_GENERATED_INPUTS)
        get_target_property(values "${target}" ${property})
        if(values)
            foreach(value IN LISTS values)
                if(NOT value MATCHES [=[^\$<]=])
                    get_filename_component(absolute "${value}" ABSOLUTE BASE_DIR "${source_dir}")
                    if(EXISTS "${absolute}")
                        list(APPEND files "${absolute}")
                    endif()
                endif()
            endforeach()
        endif()
    endforeach()
    foreach(property INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
        get_target_property(values "${target}" ${property})
        if(values)
            list(APPEND includes ${values})
        endif()
    endforeach()
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(links "${target}" ${property})
        if(links)
            foreach(link IN LISTS links)
                if(TARGET "${link}")
                    mark_collect_target_inputs("${link}" linked_files linked_includes)
                    list(APPEND files ${linked_files})
                    list(APPEND includes ${linked_includes})
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES files)
    list(REMOVE_DUPLICATES includes)
    set(${output_files} "${files}" PARENT_SCOPE)
    set(${output_includes} "${includes}" PARENT_SCOPE)
endfunction()

function(mark_target_provenance target source_root)
    get_filename_component(source_root "${source_root}" ABSOLUTE)
    mark_collect_target_inputs("${target}" pending includes)
    set(files "")
    while(pending)
        list(POP_FRONT pending file)
        file(RELATIVE_PATH relative "${source_root}" "${file}")
        if(NOT relative MATCHES [=[^\.\./]=] AND NOT file IN_LIST files)
            list(APPEND files "${file}")
            file(STRINGS "${file}" quoted_includes REGEX "^[ \t]*#[ \t]*include[ \t]+\"")
            get_filename_component(parent "${file}" DIRECTORY)
            foreach(line IN LISTS quoted_includes)
                string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]+\"([^\"]+)\".*" [=[\1]=] header "${line}")
                foreach(directory "${parent}" ${includes})
                    if(EXISTS "${directory}/${header}")
                        get_filename_component(absolute "${directory}/${header}" ABSOLUTE)
                        list(APPEND pending "${absolute}")
                        break()
                    endif()
                endforeach()
            endforeach()
        endif()
    endwhile()
    list(SORT files)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type)
    set(manifest "provenance_schema=1\ncompiler=${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\nbuild=${CMAKE_BUILD_TYPE}\nflags=${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${build_type}}\n")
    foreach(file IN LISTS files)
        file(RELATIVE_PATH relative "${source_root}" "${file}")
        file(SHA256 "${file}" hash)
        string(APPEND manifest "${relative} ${hash}\n")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${files})
    string(SHA256 hash "${manifest}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${target}-sources.manifest" "${manifest}")
    set_property(TARGET "${target}" PROPERTY MARK_SOURCE_HASH "${hash}")
endfunction()

if(TARGET mark_pose)
    set_property(TARGET vision_pose APPEND PROPERTY MARK_CMAKE_INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/g2.cmake")
endif()
if(TARGET mark_tracking)
    set_property(TARGET vision_tracking APPEND PROPERTY MARK_CMAKE_INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/g4.cmake")
endif()
set(mark_identity_consumers mark_offline marker_app decode_audit temporal_audit geometry_audit)
get_property(mark_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS mark_targets)
    get_target_property(type "${target}" TYPE)
    if(NOT type STREQUAL "INTERFACE_LIBRARY")
        mark_target_provenance("${target}" "${CMAKE_CURRENT_SOURCE_DIR}")
        if(target IN_LIST mark_identity_consumers)
            get_target_property(hash "${target}" MARK_SOURCE_HASH)
            target_compile_definitions("${target}" PRIVATE MARK_CODE_HASH="${hash}")
        endif()
    endif()
endforeach()
