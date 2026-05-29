# SPDX-License-Identifier: Apache-2.0
#
# arthook, ART method-hooking static library. Consume via
#   add_subdirectory(third_party/arthook)
#   target_link_libraries(my_so PRIVATE arthook::arthook)

cmake_minimum_required(VERSION 3.18)
project(arthook LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ARTHOOK_COMMON_SOURCES
    src/art/ArtMethod.cpp
    src/art/Layout.cpp
    src/elf/ElfResolver.cpp
    src/hook/Deoptimize.cpp
    src/hook/HookEngine.cpp
    src/hook/Hooked.cpp
    src/hook/SelfTest.cpp
    src/jvm/ClassLookup.cpp
    src/jvm/JavaVM.cpp
    src/probe/Probe.cpp
    src/trampoline/Trampoline.cpp
)

# Per-arch trampoline. Explicit dispatch so a misconfigured ABI fails fast.
if(ANDROID_ABI STREQUAL "arm64-v8a" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(ARTHOOK_TRAMP_SRC src/trampoline/trampoline_arm64.S)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
    set(ARTHOOK_TRAMP_SRC src/trampoline/trampoline_arm.S)
elseif(ANDROID_ABI STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(ARTHOOK_TRAMP_SRC src/trampoline/trampoline_x86_64.S)
elseif(ANDROID_ABI STREQUAL "x86" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "i686")
    set(ARTHOOK_TRAMP_SRC src/trampoline/trampoline_x86.S)
else()
    message(FATAL_ERROR
        "arthook: unsupported ABI/processor (ANDROID_ABI='${ANDROID_ABI}' "
        "CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}')")
endif()

add_library(arthook STATIC ${ARTHOOK_COMMON_SOURCES} ${ARTHOOK_TRAMP_SRC})
add_library(arthook::arthook ALIAS arthook)

target_include_directories(arthook
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(arthook PRIVATE
    -Wall -Wextra
    -fno-stack-protector
    -fvisibility=hidden
    -fvisibility-inlines-hidden
)
target_compile_options(arthook PRIVATE
    $<$<CONFIG:Release>:-O2 -fno-rtti -fno-exceptions>
)

option(ARTHOOK_NO_LOG "Disable __android_log_print calls" OFF)
if(ARTHOOK_NO_LOG)
    target_compile_definitions(arthook PRIVATE ARTHOOK_NO_LOG=1)
endif()

find_library(ANDROID_LOG_LIB log)
find_library(ANDROID_DL_LIB  dl)
target_link_libraries(arthook PUBLIC ${ANDROID_LOG_LIB} ${ANDROID_DL_LIB})

set_target_properties(arthook PROPERTIES
    EXPORT_NAME arthook
    OUTPUT_NAME arthook
)
