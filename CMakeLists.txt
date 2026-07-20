cmake_minimum_required(VERSION 3.20)

# -----------------------------------------------------------------------------
# Standalone RHI: 跨图形 API 的统一渲染抽象 + 三个后端实现
#   - RHIDefinitions      : 独立类型、句柄、描述、命令
#   - Core/RHIDevice      : 唯一的非虚设备门面（std::variant 分发）
#   - Vulkan/RHIVulkan    : Vulkan 后端
#   - D3D11/RHID3D11      : Direct3D 11 后端（仅 Windows）
#   - D3D12/RHID3D12      : Direct3D 12 后端（仅 Windows）
# -----------------------------------------------------------------------------
project(RHI
    VERSION 0.1.0
    DESCRIPTION "Unified RHI with Vulkan / Direct3D 11 / Direct3D 12 backends"
    LANGUAGES CXX
)

include(CTest)

# -----------------------------------------------------------------------------
# 全局编译设置
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(
        /W4            # 较高警告等级
        /permissive-   # 严格符合标准
        /Zc:__cplusplus
        /utf-8         # 源码与执行字符集统一为 UTF-8，保证中文学习注释可移植解析
    )
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# -----------------------------------------------------------------------------
# 第三方依赖
# -----------------------------------------------------------------------------
find_package(Threads REQUIRED)

# glm - 头文件库，RHIDefinitions.hpp 使用了 glm/glm.hpp
find_package(glm QUIET)
if(NOT glm_FOUND)
    # 退化路径：未安装 glm 的 find 包时，尝试用常见位置
    find_path(GLM_INCLUDE_DIR glm/glm.hpp
        HINTS
            ${GLM_ROOT}
            $ENV{GLM_ROOT}
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm-1.0.1
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm
        PATH_SUFFIXES glm include
        REQUIRED)
    add_library(glm_iface INTERFACE)
    add_library(glm::glm ALIAS glm_iface)
    target_include_directories(glm_iface INTERFACE ${GLM_INCLUDE_DIR})
    message(STATUS "glm: using fallback include at ${GLM_INCLUDE_DIR}")
endif()

# Vulkan SDK
find_package(Vulkan REQUIRED)

# -----------------------------------------------------------------------------
# 目标：RHI（静态库）
# -----------------------------------------------------------------------------
add_library(RHI STATIC)

# .inl 只由对应 .cpp 按固定顺序 include，不单独编译；但仍把它们登记为 target 的
# HEADER_FILE_ONLY 源文件。这样 CMake Tools 能把 RHI 的 C++20、宏和 include path
# 提供给 VSCode IntelliSense，直接打开 .inl 时不会退回到错误的默认解析环境。
set(RHI_HEADER_AND_INLINE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/RHI.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RHIDefinitions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/RHIDevice.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/RHIDeviceDesc.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/RHIDeviceFactory.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderGraph/RHIRenderGraph.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/RHIVulkan.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/RHIVulkanPrivate.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/RHIVulkanCore.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/RHIVulkanResources.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/RHIVulkanPipelines.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/RHIVulkanSyncSwapchain.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/RHIVulkanFrame.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/RHIVulkanDestroy.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D11/RHID3D11.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D11/RHID3D11Private.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D11/RHID3D11Core.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D11/RHID3D11Resources.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D11/RHID3D11Pipelines.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D11/RHID3D11SyncSwapchain.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D11/RHID3D11Frame.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D11/RHID3D11Destroy.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/RHID3D12.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/RHID3D12Private.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/RHID3D12Core.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/RHID3D12Resources.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/RHID3D12Pipelines.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/RHID3D12SyncSwapchain.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/RHID3D12Frame.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/RHID3D12Destroy.inl
)
set_source_files_properties(
    ${RHI_HEADER_AND_INLINE_FILES}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

# 显式列出所有 .cpp。RHIDevice.cpp 用 std::variant 包含三个后端实例，缺一不可。
target_sources(RHI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/RHIDefinitions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderGraph/RHIRenderGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/RHIDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/RHIDeviceFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/RHIVulkan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D11/RHID3D11.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/D3D12/RHID3D12.cpp
    ${RHI_HEADER_AND_INLINE_FILES}
)

# 在 Visual Studio/CMake Tools 中保持和磁盘一致的目录结构，便于定位后端分片。
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${RHI_HEADER_AND_INLINE_FILES})

# PUBLIC 头文件目录：让上层能直接 #include "RHI.hpp" / "RHI/Core/..." 等
target_include_directories(RHI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接公共依赖
target_link_libraries(RHI
    PUBLIC
        glm::glm
        Vulkan::Vulkan
        Threads::Threads
)

# Direct3D 后端（仅 Windows；源代码已经用 #if defined(_WIN32) 保护）
if(WIN32)
    target_link_libraries(RHI
        PUBLIC
            d3d11       # ID3D11Device, ID3D11DeviceContext, ID3D11Buffer, ...
            d3d12       # ID3D12Device, ID3D12CommandQueue, ...
            dxgi        # IDXGIFactory*, IDXGISwapChain, ...
            dxguid      # IID_ID3D11Debug, WKPDID_D3DDebugObjectName
            d3dcompiler # D3DCompile / D3DReflect（RHID3D11 用）
    )
endif()

# 库别名 RHI::RHI，调用方可以直接 target_link_libraries(... RHI::RHI)
add_library(RHI::RHI ALIAS RHI)

# -----------------------------------------------------------------------------
# PBR Demo（默认开启，可用 -DRHI_BUILD_DEMOS=OFF 关闭）
# -----------------------------------------------------------------------------
option(RHI_BUILD_DEMOS "Build RHI usage demos (e.g. PBRDemo)" ON)
if(RHI_BUILD_DEMOS)
    add_subdirectory(PBRDemo)
endif()

# -----------------------------------------------------------------------------
# 可选：不依赖窗口和 GPU 的 RenderGraph 编译测试
# -----------------------------------------------------------------------------
option(RHI_BUILD_TESTS "Build RHI unit tests" ON)
if(BUILD_TESTING AND RHI_BUILD_TESTS)
    add_executable(RHIRenderGraphTests
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/RHIRenderGraphTests.cpp
    )
    target_link_libraries(RHIRenderGraphTests PRIVATE RHI::RHI)
    target_compile_features(RHIRenderGraphTests PRIVATE cxx_std_20)
    add_test(NAME RHIRenderGraphTests COMMAND RHIRenderGraphTests)

    add_executable(RHIBackendGraphTests
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/RHIBackendGraphTests.cpp
    )
    target_link_libraries(RHIBackendGraphTests PRIVATE RHI::RHI)
    target_compile_features(RHIBackendGraphTests PRIVATE cxx_std_20)
    add_test(NAME RHIBackendGraphTests COMMAND RHIBackendGraphTests)
endif()

# -----------------------------------------------------------------------------
# 状态输出
# -----------------------------------------------------------------------------
message(STATUS "========================================")
message(STATUS "RHI ${PROJECT_VERSION}")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ std     : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Platform    : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Vulkan      : ${Vulkan_VERSION} (${VULKAN_LIBRARY})")
if(glm_FOUND)
    message(STATUS "  glm         : (found via find_package)")
else()
    message(STATUS "  glm         : ${GLM_INCLUDE_DIR} (fallback)")
endif()
if(WIN32)
    set(RHI_D3D_BACKENDS_STATUS "ON")
else()
    set(RHI_D3D_BACKENDS_STATUS "OFF")
endif()
message(STATUS "  D3D backends: ${RHI_D3D_BACKENDS_STATUS}")
message(STATUS "========================================")
