project(cpp_tm_unit_test)

enable_testing()
include(GoogleTest)

find_package(GTest REQUIRED CONFIG)

add_executable(${PROJECT_NAME} ./cpp/unit_test.cpp)
target_link_libraries(${PROJECT_NAME} GTest::GTest cpp_tm)

gtest_discover_tests(${PROJECT_NAME})