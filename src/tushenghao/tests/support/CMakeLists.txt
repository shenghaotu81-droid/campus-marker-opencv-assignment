add_library(mark_test_support INTERFACE)
target_include_directories(mark_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/detection" "${CMAKE_CURRENT_SOURCE_DIR}/detection/temporal"
    "${CMAKE_CURRENT_SOURCE_DIR}/detection/corners" "${CMAKE_CURRENT_SOURCE_DIR}/observability")
target_compile_definitions(mark_test_support INTERFACE VISION_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
