cmake_minimum_required(VERSION 3.20)
project(ArchetypeECS LANGUAGES CXX)

include(CTest)
find_package(Threads REQUIRED)

add_library(ECS SHARED
    ECS/ECS.hpp
    ECS/ECSApi.hpp
    ECS/Memory.hpp
    ECS/Memory.cpp
    ECS/LinearArena.hpp
    ECS/LinearArena.cpp
    ECS/JobSystem.hpp
    ECS/JobSystem.cpp
    ECS/Entity.hpp
    ECS/Component.hpp
    ECS/Component.cpp
    ECS/Signature.hpp
    ECS/Signature.cpp
    ECS/Filter.hpp
    ECS/Chunk.hpp
    ECS/Chunk.cpp
    ECS/Archetype.hpp
    ECS/Archetype.cpp
    ECS/World.hpp
    ECS/World.cpp
    ECS/Query.hpp
    ECS/CommandBuffer.hpp
    ECS/CommandBuffer.cpp
    ECS/System.hpp
    ECS/System.cpp
    ECS/README.md
)

target_include_directories(ECS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ECS PUBLIC cxx_std_20)
target_link_libraries(ECS PUBLIC Threads::Threads)
target_compile_definitions(ECS
    PRIVATE ECS_EXPORTS
    PUBLIC ECS_SHARED
)

if(MSVC)
    target_compile_options(ECS PRIVATE /utf-8 /W4 /permissive- /FS)
else()
    target_compile_options(ECS PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(ECSTests ECS/Tests/ECSTests.cpp)
    target_link_libraries(ECSTests PRIVATE ECS)
    target_compile_features(ECSTests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(ECSTests PRIVATE /utf-8 /W4 /permissive- /FS)
    else()
        target_compile_options(ECSTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME ArchetypeECS COMMAND ECSTests)

    add_library(ECSCrossDllPlugin SHARED
        ECS/Tests/CrossDllComponents.hpp
        ECS/Tests/CrossDllPlugin.cpp
    )
    target_link_libraries(ECSCrossDllPlugin PRIVATE ECS)
    target_compile_features(ECSCrossDllPlugin PRIVATE cxx_std_20)
    target_compile_definitions(ECSCrossDllPlugin PRIVATE ECS_CROSS_DLL_PLUGIN_EXPORTS)

    add_executable(ECSCrossDllTests
        ECS/Tests/CrossDllComponents.hpp
        ECS/Tests/CrossDllTests.cpp
    )
    target_link_libraries(ECSCrossDllTests PRIVATE ECS ECSCrossDllPlugin)
    target_compile_features(ECSCrossDllTests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(
            ECSCrossDllPlugin PRIVATE /utf-8 /W4 /permissive- /FS)
        target_compile_options(
            ECSCrossDllTests PRIVATE /utf-8 /W4 /permissive- /FS)
    else()
        target_compile_options(ECSCrossDllPlugin PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(ECSCrossDllTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME ECSCrossDll COMMAND ECSCrossDllTests)
endif()

add_executable(ECSBenchmark ECS/Benchmarks/ECSBenchmark.cpp)
target_link_libraries(ECSBenchmark PRIVATE ECS)
target_compile_features(ECSBenchmark PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(ECSBenchmark PRIVATE /utf-8 /W4 /permissive- /FS)
else()
    target_compile_options(ECSBenchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
