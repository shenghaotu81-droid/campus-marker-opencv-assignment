# 低层数学仅消费类型与 OpenCV；配置和跟踪分别链接，不反向依赖算法。
add_library(vision_math covariance/covariance_validation.cpp)
target_link_libraries(vision_math PUBLIC vision_types ${OpenCV_LIBS})
