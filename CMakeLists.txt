cmake_minimum_required(VERSION 3.10)
project(minitrace)

add_library(${PROJECT_NAME} STATIC minitrace.c)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

option(MTR_ENABLED "Enable minitrace" ON)
if(MTR_ENABLED)
    target_compile_definitions(${PROJECT_NAME} PUBLIC MTR_ENABLED)
endif()

option(MTR_BUILD_TEST "Build test apps" OFF)
if(MTR_BUILD_TEST)
    add_executable(minitrace_test minitrace_test.cpp)
    target_link_libraries(minitrace_test ${PROJECT_NAME})

    find_package(Threads)
    add_executable(minitrace_test_mt minitrace_test_mt.cpp)
    target_link_libraries(minitrace_test_mt ${PROJECT_NAME} Threads::Threads)
endif()