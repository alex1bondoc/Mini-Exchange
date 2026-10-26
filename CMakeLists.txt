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

add_executable(ConversionTests tests/ConversionTests.cpp)
target_link_libraries(ConversionTests PRIVATE common GTest::gtest_main)

add_executable(FramerTests tests/FramerTests.cpp)
target_link_libraries(FramerTests PRIVATE common GTest::gtest_main)

add_executable(PoolTests tests/PoolTests.cpp)
target_link_libraries(PoolTests PRIVATE common GTest::gtest_main)

add_executable(OrderBookTests tests/OrderBookTests.cpp)
target_link_libraries(OrderBookTests PRIVATE common GTest::gtest_main)

add_executable(MatcherTests tests/MatcherTests.cpp)
target_link_libraries(MatcherTests PRIVATE common GTest::gtest_main)

add_executable(EngineTests tests/EngineTests.cpp)
target_link_libraries(EngineTests PRIVATE common GTest::gtest_main)

add_executable(SessionMessagesTests tests/SessionMessagesTests.cpp)
target_link_libraries(SessionMessagesTests PRIVATE common GTest::gtest_main)

include(GoogleTest) 
gtest_discover_tests(WriterTests)
gtest_discover_tests(ParserTests)
gtest_discover_tests(ConversionTests)
gtest_discover_tests(FramerTests)
gtest_discover_tests(PoolTests)
gtest_discover_tests(OrderBookTests)
gtest_discover_tests(MatcherTests)
gtest_discover_tests(EngineTests)
gtest_discover_tests(SessionMessagesTests)

