cmake_minimum_required(VERSION 3.20)

# -----------------------------------------------------------------------------
# PBR Demo: 一个 plane + 一个 UV sphere 的简化 PBR 渲染示例
#   - 窗口 surface 由 Win32/Vulkan 平台层创建，所有资源和绘制都经过 RHI + RenderGraph
#   - shader 在 build 时用 glslangValidator 编译为 SPIR-V
# -----------------------------------------------------------------------------
project(PBRDemo LANGUAGES CXX)

# -----------------------------------------------------------------------------
# 依赖
# -----------------------------------------------------------------------------
find_package(Vulkan REQUIRED)
# find_package 可能从 vcpkg 找 lib 但 SDK 头在 VULKAN_SDK；显式补 include 路径
if(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Include/vulkan/vulkan.h")
    message(STATUS "PBRDemo: using VULKAN_SDK headers at $ENV{VULKAN_SDK}/Include")
    set(VULKAN_INCLUDE_DIRS $ENV{VULKAN_SDK}/Include)
endif()
find_package(glm   REQUIRED)
find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS $ENV{VULKAN_SDK}/bin
    PATH_SUFFIXES bin
    DOC   "GLSL → SPIR-V compiler from Vulkan SDK"
)
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found. Install Vulkan SDK and ensure it's on PATH or set VULKAN_SDK env var.")
endif()

# -----------------------------------------------------------------------------
# 可执行文件
# -----------------------------------------------------------------------------
add_executable(PBRDemo WIN32 main.cpp)

# shader 输出目录：<build>/PBRDemo/shaders/
set(PBR_SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${PBR_SHADER_OUT_DIR})

# 生成源码可见的配置头。相比只用 target_compile_definitions 注入宏，这种方式让
# 编译器和 IntelliSense 看到同一个 constexpr 定义，并支持“转到定义”。
set(PBR_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PBR_GENERATED_INCLUDE_DIR})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/PBRDemoConfig.hpp.in
    ${PBR_GENERATED_INCLUDE_DIR}/PBRDemoConfig.hpp
    @ONLY
)
set_source_files_properties(
    ${PBR_GENERATED_INCLUDE_DIR}/PBRDemoConfig.hpp
    PROPERTIES
        GENERATED TRUE
        HEADER_FILE_ONLY TRUE
)
target_sources(PBRDemo PRIVATE
    ${PBR_GENERATED_INCLUDE_DIR}/PBRDemoConfig.hpp
)
target_include_directories(PBRDemo PRIVATE
    ${PBR_GENERATED_INCLUDE_DIR}
)

# shader 编译：.vert / .frag → .spv
function(rhi_compile_shader SRC_FILE)
    get_filename_component(SRC_NAME ${SRC_FILE} NAME)
    set(OUT_FILE ${PBR_SHADER_OUT_DIR}/${SRC_NAME}.spv)
    add_custom_command(
        OUTPUT  ${OUT_FILE}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SRC_FILE} -o ${OUT_FILE}
        DEPENDS ${SRC_FILE}
        COMMENT "Compiling ${SRC_NAME} → SPIR-V"
        VERBATIM)
endfunction()

rhi_compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.vert)
rhi_compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.frag)
rhi_compile_shader(${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert)

# D3D11/D3D12 在运行时通过 D3DCompileFromFile 编译同一份 HLSL。复制到统一输出目录，
# 让三个后端都只依赖 PBRDemoConfig.hpp 提供的 SHADER_DIRECTORY。
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.hlsl
    ${PBR_SHADER_OUT_DIR}/pbr.hlsl
    COPYONLY
)

# 把 .spv 列成 PBRDemo 的源文件 → CMake 自动建立编译顺序
target_sources(PBRDemo PRIVATE
    ${PBR_SHADER_OUT_DIR}/pbr.vert.spv
    ${PBR_SHADER_OUT_DIR}/pbr.frag.spv
    ${PBR_SHADER_OUT_DIR}/shadow.vert.spv
)

# -----------------------------------------------------------------------------
# 链接
# -----------------------------------------------------------------------------
target_link_libraries(PBRDemo
    PRIVATE
        RHI::RHI
)

if(VULKAN_INCLUDE_DIRS)
    target_include_directories(PBRDemo PRIVATE ${VULKAN_INCLUDE_DIRS})
endif()

if(MSVC)
    target_compile_options(PBRDemo PRIVATE
        /W4 /permissive- /Zc:__cplusplus
    )
    target_compile_definitions(PBRDemo PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX
    )
else()
    target_compile_options(PBRDemo PRIVATE
        -Wall -Wextra
    )
endif()

set_target_properties(PBRDemo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
