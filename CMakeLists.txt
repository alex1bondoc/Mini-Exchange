cmake_minimum_required(VERSION 3.23)
project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(GTest REQUIRED)

add_library(common INTERFACE)
target_include_directories(common INTERFACE ${CMAKE_SOURCE_DIR})

enable_testing()
add_executable(WriterTests tests/WriterTests.cpp)
target_link_libraries(WriterTests PRIVATE common GTest::gtest_main) 

add_executable(ParserTests tests/ParserTests.cpp)
target_link_libraries(ParserTests PRIVATE common GTest::gtest_main)

include(GoogleTest) 
gtest_discover_tests(WriterTests)
gtest_discover_tests(ParserTests)
