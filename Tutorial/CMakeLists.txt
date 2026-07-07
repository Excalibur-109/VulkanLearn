cmake_minimum_required(VERSION 3.20)
project(VulkanTutorial LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
    add_compile_definitions(NOMINMAX)
endif()

# main.cpp 跟 CMakeLists.txt 同级，直接列出来
add_executable(VulkanTutorial main.cpp)

find_package(Vulkan        REQUIRED)         # Module 模式：CMake 自带 FindVulkan 模块，依赖 VULKAN_SDK 环境变量
find_package(glfw3         CONFIG REQUIRED)
find_package(glm           CONFIG REQUIRED)
find_package(stb           REQUIRED)         # Module 模式：vcpkg 的 stb 只提供 FindStb.cmake，且是 header-only
find_package(tinyobjloader REQUIRED)

target_include_directories(VulkanTutorial PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # main.cpp 在当前目录
    ${GLM_INCLUDE_DIRS}
    ${Stb_INCLUDE_DIR}                    # stb 头文件目录（FindStb 提供的变量）
)

target_link_libraries(VulkanTutorial PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    tinyobjloader::tinyobjloader
    # stb 是 header-only，已经在 include 目录里，不要再 link target
)

if(WIN32)
    # glfw3.dll 来自 vcpkg（按 $<CONFIG> 选 debug/release 子目录）
    # vulkan-1.dll 不需要 copy：Windows 显卡驱动已部署在 C:\Windows\System32\
    add_custom_command(TARGET VulkanTutorial POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<CONFIG>/bin/glfw3.dll"
            $<TARGET_FILE_DIR:VulkanTutorial>
    )
endif()