project(
	cpp_tm
	VERSION 0.1.0
	DESCRIPTION "C++ thread manager using standard library threads." 
)
cmake_minimum_required(VERSION 3.19.0)

add_library(${PROJECT_NAME} INTERFACE ./include/cpp_tm.h)
target_include_directories(${PROJECT_NAME} INTERFACE ./include)

add_subdirectory("test" "test")