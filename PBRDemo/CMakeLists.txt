cmake_minimum_required(VERSION 3.20)

# -----------------------------------------------------------------------------
# PBR Demo: 一个 plane + 一个 UV sphere 的简化 PBR 渲染示例
#   - 直接用 Vulkan API（不经过 RHI）展示底层用法
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

# 把 .spv 列成 PBRDemo 的源文件 → CMake 自动建立编译顺序
target_sources(PBRDemo PRIVATE
    ${PBR_SHADER_OUT_DIR}/pbr.vert.spv
    ${PBR_SHADER_OUT_DIR}/pbr.frag.spv
)

# 注入 shader 绝对路径
target_compile_definitions(PBRDemo PRIVATE
    PBR_SHADER_DIR="${PBR_SHADER_OUT_DIR}"
)

# -----------------------------------------------------------------------------
# 链接
# -----------------------------------------------------------------------------
target_link_libraries(PBRDemo
    PRIVATE
        Vulkan::Vulkan
        glm::glm
)

if(VULKAN_INCLUDE_DIRS)
    target_include_directories(PBRDemo PRIVATE ${VULKAN_INCLUDE_DIRS})
endif()

if(MSVC)
    target_compile_options(PBRDemo PRIVATE
        /W4 /permissive- /Zc:__cplusplus
        /source-charset:utf-8
        /execution-charset:utf-8
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
