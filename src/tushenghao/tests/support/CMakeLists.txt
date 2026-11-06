add_library(mark_test_support INTERFACE)
target_include_directories(mark_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/detection"
)
target_compile_definitions(mark_test_support INTERFACE VISION_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
