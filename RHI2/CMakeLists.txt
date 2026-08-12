cmake_minimum_required(VERSION 3.20)
project(rhi_pbr_demo LANGUAGES CXX)

option(RHI_ENABLE_VULKAN "Build Vulkan adapter when the SDK is available" ON)
option(RHI_ENABLE_D3D11 "Build D3D11 adapter on Windows" ON)
option(RHI_ENABLE_D3D12 "Build D3D12 adapter on Windows" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep the runnable program in a predictable location for both single- and
# multi-configuration generators (Visual Studio puts Release under a config
# subdirectory unless these variables are set explicitly).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${cfg}" cfg_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/bin")
endforeach()

add_library(rhi STATIC
  src/rhi/RHI.cpp
  src/backends/SoftwareBackend.cpp
  src/backends/VulkanBackend.cpp
  src/backends/D3D11Backend.cpp
  src/backends/D3D12Backend.cpp)
target_include_directories(rhi PUBLIC include)
target_compile_definitions(rhi PUBLIC
  RHI_HAS_VULKAN=$<BOOL:${RHI_ENABLE_VULKAN}>
  RHI_HAS_D3D11=$<AND:$<BOOL:${RHI_ENABLE_D3D11}>,$<PLATFORM_ID:Windows>>
  RHI_HAS_D3D12=$<AND:$<BOOL:${RHI_ENABLE_D3D12}>,$<PLATFORM_ID:Windows>>)

if (RHI_ENABLE_VULKAN)
  find_package(Vulkan QUIET)
  if (Vulkan_FOUND)
    target_link_libraries(rhi PUBLIC Vulkan::Vulkan)
    target_compile_definitions(rhi PUBLIC RHI_VULKAN_SDK=1)
  else()
    message(STATUS "Vulkan SDK not found: Vulkan adapter uses the documented stub path")
  endif()
endif()

if (WIN32)
  if (RHI_ENABLE_D3D11)
    target_link_libraries(rhi PUBLIC d3d11 dxgi d3dcompiler)
  endif()
  if (RHI_ENABLE_D3D12)
    target_link_libraries(rhi PUBLIC d3d12 dxgi dxguid d3dcompiler)
  endif()
endif()

add_executable(pbr_demo
  examples/pbr_demo/main.cpp
  examples/pbr_demo/PbrMath.cpp
  examples/pbr_demo/PlaneMesh.cpp
  examples/pbr_demo/SphereMesh.cpp
  examples/pbr_demo/SoftwareRenderer.cpp
  examples/pbr_demo/Win32Window.cpp)
target_link_libraries(pbr_demo PRIVATE rhi)
if (WIN32)
  target_link_libraries(pbr_demo PRIVATE user32 gdi32)
endif()

if (MSVC)
  # 中文注释以 UTF-8 保存；显式指定源文件和执行字符集，避免 MSVC
  # 按系统代码页解析中文字节而产生误报或语法错误。
  target_compile_options(rhi PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(pbr_demo PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(rhi PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(pbr_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()
