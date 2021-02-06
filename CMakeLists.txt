cmake_minimum_required(VERSION 3.19.0)

project(
	cpp_tm
	VERSION 0.1.0
	DESCRIPTION "C++ thread manager using standard library threads." 
)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} INTERFACE ./include/cpp_tm.h)
target_include_directories(${PROJECT_NAME} INTERFACE ./include)

set(CPPTM_UNIT_TESTS ON CACHE BOOL "Should add unit test solution to the project")

add_subdirectory("test" "test")
