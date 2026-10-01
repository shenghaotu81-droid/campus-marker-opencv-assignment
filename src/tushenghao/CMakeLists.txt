cmake_minimum_required(VERSION 3.10)

project(marker_detector)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_executable(marker_detector
    src/main.cpp
)

target_link_libraries(marker_detector
    PRIVATE
    ${OpenCV_LIBS}
)

target_include_directories(marker_detector
    PRIVATE
    include
)