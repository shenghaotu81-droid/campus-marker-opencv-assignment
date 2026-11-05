# 值契约不链接检测／位姿／跟踪实现；各模块显式消费此目标。
add_library(vision_types INTERFACE)
target_include_directories(vision_types INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
target_compile_features(vision_types INTERFACE cxx_std_17)
target_link_libraries(vision_types INTERFACE ${OpenCV_LIBS})
