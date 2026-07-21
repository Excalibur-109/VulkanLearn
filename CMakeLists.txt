cmake_minimum_required(VERSION 3.20)

project(MathLibrary VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

option(MATH_BUILD_BENCHMARKS "Build the standalone Math performance benchmarks" OFF)
option(MATH_ENABLE_SIMD "Enable architecture-specific Math SIMD paths" ON)

add_library(Math INTERFACE)
target_sources(Math INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Math.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Math.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Scalar.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Vector.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Functions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Curves.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Color.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Matrix.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Quaternion.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Random.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Math/Rendering.hpp
)
target_include_directories(Math INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(Math INTERFACE cxx_std_20)
if(NOT MATH_ENABLE_SIMD)
    target_compile_definitions(Math INTERFACE MATH_DISABLE_SIMD=1)
endif()
add_library(Math::Math ALIAS Math)

if(MSVC)
    target_compile_options(Math INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(Math INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(MathTests
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/MathTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/NamespaceOptOutTests.cpp
    )
    target_link_libraries(MathTests PRIVATE Math::Math)
    add_test(NAME MathTests COMMAND MathTests)
endif()

if(MATH_BUILD_BENCHMARKS)
    add_executable(MathBenchmarks
        ${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks/MathBenchmarks.cpp
    )
    target_link_libraries(MathBenchmarks PRIVATE Math::Math)
endif()
