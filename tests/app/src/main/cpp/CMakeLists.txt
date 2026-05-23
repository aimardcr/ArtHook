# Test app — consumes arthook from five directories up.

cmake_minimum_required(VERSION 3.22.1)
project("arthooktest")

set(ARTHOOK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
get_filename_component(ARTHOOK_ROOT "${ARTHOOK_ROOT}" ABSOLUTE)

add_subdirectory("${ARTHOOK_ROOT}" "${CMAKE_BINARY_DIR}/arthook")

add_library(${CMAKE_PROJECT_NAME} SHARED
        native-lib.cpp
        test_hooks.cpp)

target_link_libraries(${CMAKE_PROJECT_NAME}
        arthook::arthook
        android
        log)
