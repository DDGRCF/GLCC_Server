cmake_minimum_required(VERSION 3.22)
project(glcc_server)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fconcepts --std=c++14")

find_package(OpenCV REQUIRED)
include_directories($(OpenCV_INCLUDE_DIRS))
set(EXTRA_LIBS ${EXTRA_LIBS} ${OpenCV_LIBS})
if (FOUND_OpenCV)
    message(STATUS "OpenCV Library status: ")
    message(STATUS "    version: {OpenCV_VERSION}" \n)
endif()

# mmdeploy
find_package(MMDeploy REQUIRED)

# custom
include_directories(${PROJECT_SOURCE_DIR}/include)
aux_source_directory(${PROJECT_SOURCE_DIR}/src DIR_SRCS)

add_executable(${CMAKE_PROJECT_NAME} ${DIR_SRCS})
add_subdirectory(bytetrack)
include_directories(${bytetrack_SOURCE_DIR}/include)

# eigen3
find_package(Eigen3 REQUIRED)
include_directories(${EIGEN3_INCLUDE_DIRS})

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})
set(EXTRA_LIBS ${EXTRA_LIBS} jsoncpp workflow bytetrack)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--disable-new-dtags)
mmdeploy_load_static(${CMAKE_PROJECT_NAME} MMDeployStaticModules)
mmdeploy_load_dynamic(${CMAKE_PROJECT_NAME} MMDeployDynamicModules)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE MMDeployLibs ${EXTRA_LIBS})


add_definitions(-O0 -pthread)
