cmake_minimum_required(VERSION 3.24)
project(gt_api_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(httplib CONFIG REQUIRED)
message(STATUS "cpp-httplib found: ${httplib_VERSION}")

add_executable(gt_api_test src/main.cpp)
target_link_libraries(gt_api_test PRIVATE httplib::httplib)
