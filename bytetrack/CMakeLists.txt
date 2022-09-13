cmake_minimum_required(VERSION 3.22)

project(bytetrack)

add_definitions(-std=c++14)

option(CUDA_USE_STATIC_CUDA_RUNTIME OFF)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_BUILD_TYPE Debug)

find_package(Eigen3 REQUIRED)
include_directories(${EIGEN3_INCLUDE_DIRS})

find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

include_directories(${PROJECT_SOURCE_DIR}/include)
link_directories(${PROJECT_SOURCE_DIR}/include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall -Ofast -Wfatal-errors")


file(GLOB mSOURCE_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp)
add_library(bytetrack SHARED ${mSOURCE_FILES})
target_link_libraries(bytetrack ${OpenCV_LIBS})
add_definitions(-O2 -pthread)