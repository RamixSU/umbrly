cmake_minimum_required(VERSION 3.22.1)

project(umbrly_android)

set(UMBRLY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../)

add_library(
    umbrly_android
    SHARED
    android_runner.cpp
    ${UMBRLY_ROOT}/src/lexer.cpp
    ${UMBRLY_ROOT}/src/parser.cpp
    ${UMBRLY_ROOT}/src/ast.cpp
    ${UMBRLY_ROOT}/src/interpreter.cpp
    ${UMBRLY_ROOT}/src/builtins.cpp
)

target_include_directories(
    umbrly_android
    PRIVATE
    ${UMBRLY_ROOT}/include
)

target_compile_features(umbrly_android PRIVATE cxx_std_17)

find_library(log-lib log)
target_link_libraries(umbrly_android ${log-lib})
