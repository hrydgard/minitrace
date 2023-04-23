cmake_minimum_required(VERSION 3.10)
project(minitrace)

option(MTR_BUILD_SHARED "Build minitrace as a shared library" ${BUILD_SHARED_LIBS})
if (${MTR_BUILD_SHARED})
    set(MTR_LIB_TYPE "SHARED")
else()
    set(MTR_LIB_TYPE "STATIC")
endif()
add_library(${PROJECT_NAME} ${MTR_LIB_TYPE} minitrace.c)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

option(MTR_ENABLED "Enable minitrace" ON)
if(MTR_ENABLED)
    target_compile_definitions(${PROJECT_NAME} PUBLIC MTR_ENABLED)
endif()

include(GenerateExportHeader)
generate_export_header("${PROJECT_NAME}")
target_include_directories(${PROJECT_NAME} PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>")

option(MTR_BUILD_TEST "Build test apps" OFF)
if(MTR_BUILD_TEST)
    add_executable(minitrace_test minitrace_test.cpp)
    target_link_libraries(minitrace_test ${PROJECT_NAME})

    find_package(Threads)
    add_executable(minitrace_test_mt minitrace_test_mt.cpp)
    target_link_libraries(minitrace_test_mt ${PROJECT_NAME} Threads::Threads)
endif()

target_include_directories(${PROJECT_NAME} INTERFACE $<INSTALL_INTERFACE:include>)

install(TARGETS ${PROJECT_NAME} EXPORT minitrace DESTINATION lib)
install(FILES minitrace.h "${CMAKE_CURRENT_BINARY_DIR}/minitrace_export.h" DESTINATION include)
install(EXPORT minitrace NAMESPACE minitrace:: FILE "${PROJECT_NAME}Targets.cmake" DESTINATION "share/${PROJECT_NAME}")

include(CMakePackageConfigHelpers)
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
        INSTALL_DESTINATION "share/${PROJECT_NAME}"
        NO_SET_AND_CHECK_MACRO
        NO_CHECK_REQUIRED_COMPONENTS_MACRO)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake" DESTINATION "share/${PROJECT_NAME}")
