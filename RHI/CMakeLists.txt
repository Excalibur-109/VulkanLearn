cmake_minimum_required(VERSION 3.20)

project(RHI LANGUAGES CXX)

include(GNUInstallDirs)

option(RHI_BUILD_EXAMPLES "Build RHI examples." ON)
option(RHI_BUILD_VULKAN_BACKEND "Build the Vulkan backend module when the SDK is available." ON)
option(RHI_BUILD_D3D12_BACKEND "Build the Direct3D 12 backend module on Windows." ON)
option(RHI_BUILD_D3D11_BACKEND "Build the Direct3D 11 backend module on Windows." ON)

add_library(rhi STATIC
    Source/RHIBackendRegistry.cpp
    Source/RHIContext.cpp
    Source/RHIPBRRenderer.cpp
    Source/RHIRenderGraph.cpp
)

add_library(rhi::rhi ALIAS rhi)

target_compile_features(rhi PUBLIC cxx_std_20)

target_include_directories(rhi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    add_compile_options(/utf-8)
    target_compile_options(rhi PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(rhi PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RHI_BUILD_EXAMPLES)
    add_executable(rhi_compile_check Examples/CompileCheck.cpp)
    target_link_libraries(rhi_compile_check PRIVATE rhi::rhi)
endif()

if(WIN32 AND RHI_BUILD_D3D11_BACKEND)
    add_library(rhi_d3d11 STATIC Backends/D3D11/RHID3D11Backend.cpp)
    add_library(rhi::d3d11 ALIAS rhi_d3d11)
    target_compile_features(rhi_d3d11 PUBLIC cxx_std_20)
    target_include_directories(rhi_d3d11 PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>)
    target_link_libraries(rhi_d3d11 PUBLIC rhi::rhi d3d11 d3dcompiler dxgi)
endif()

if(WIN32 AND RHI_BUILD_D3D12_BACKEND)
    add_library(rhi_d3d12 STATIC Backends/D3D12/RHID3D12Backend.cpp)
    add_library(rhi::d3d12 ALIAS rhi_d3d12)
    target_compile_features(rhi_d3d12 PUBLIC cxx_std_20)
    target_include_directories(rhi_d3d12 PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>)
    target_link_libraries(rhi_d3d12 PUBLIC rhi::rhi d3d12 dxgi)
endif()

if(RHI_BUILD_VULKAN_BACKEND)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        add_library(rhi_vulkan STATIC Backends/Vulkan/RHIVulkanBackend.cpp)
        add_library(rhi::vulkan ALIAS rhi_vulkan)
        target_compile_features(rhi_vulkan PUBLIC cxx_std_20)
        target_include_directories(rhi_vulkan PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>)
        target_link_libraries(rhi_vulkan PUBLIC rhi::rhi Vulkan::Vulkan)
    else()
        message(STATUS "Vulkan SDK was not found; rhi_vulkan will not be built.")
    endif()
endif()

if(TARGET rhi_vulkan)
    find_program(RHI_GLSLC_EXECUTABLE NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin")
    if(RHI_GLSLC_EXECUTABLE)
        set(RHI_PBR_SHADER_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
        set(RHI_PBR_VERTEX_SPIRV "${RHI_PBR_SHADER_OUTPUT_DIRECTORY}/RHIPBR.vert.spv")
        set(RHI_PBR_FRAGMENT_SPIRV "${RHI_PBR_SHADER_OUTPUT_DIRECTORY}/RHIPBR.frag.spv")
        add_custom_command(
            OUTPUT "${RHI_PBR_VERTEX_SPIRV}" "${RHI_PBR_FRAGMENT_SPIRV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${RHI_PBR_SHADER_OUTPUT_DIRECTORY}"
            COMMAND "${RHI_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -o "${RHI_PBR_VERTEX_SPIRV}" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/RHIPBR.vert"
            COMMAND "${RHI_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -o "${RHI_PBR_FRAGMENT_SPIRV}" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/RHIPBR.frag"
            DEPENDS Shaders/RHIPBR.vert Shaders/RHIPBR.frag
            VERBATIM
        )
        add_custom_target(rhi_pbr_spirv DEPENDS "${RHI_PBR_VERTEX_SPIRV}" "${RHI_PBR_FRAGMENT_SPIRV}")
    else()
        message(STATUS "glslc was not found; Vulkan PBR shader validation executable will not be built.")
    endif()
endif()

if(RHI_BUILD_EXAMPLES AND TARGET rhi_d3d11 AND TARGET rhi_d3d12 AND TARGET rhi_vulkan)
    add_executable(rhi_native_backend_check Examples/NativeBackendCheck.cpp)
    target_link_libraries(rhi_native_backend_check PRIVATE rhi::rhi rhi::d3d11 rhi::d3d12 rhi::vulkan)
    if(TARGET rhi_pbr_spirv)
        add_executable(rhi_pbr_offscreen_check Examples/PBROffscreenCheck.cpp)
        target_link_libraries(rhi_pbr_offscreen_check PRIVATE rhi::rhi rhi::d3d11 rhi::d3d12 rhi::vulkan)
        target_compile_definitions(rhi_pbr_offscreen_check PRIVATE RHI_PBR_SPIRV_DIRECTORY="${RHI_PBR_SHADER_OUTPUT_DIRECTORY}")
        add_dependencies(rhi_pbr_offscreen_check rhi_pbr_spirv)
    endif()
endif()

install(TARGETS rhi
    EXPORT RHITargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY Include/RHI DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT RHITargets
    FILE RHITargets.cmake
    NAMESPACE rhi::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RHI
)
