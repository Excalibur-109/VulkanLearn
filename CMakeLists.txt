cmake_minimum_required(VERSION 3.20)
project(VulkanLearnExamples LANGUAGES CXX)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin"
)

if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "PbrShadowExample requires glslangValidator. Please install the Vulkan SDK and set VULKAN_SDK.")
endif()

set(PBR_SHADOW_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/shadow_depth.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/shadow_depth.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/pbr_lit.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/pbr_lit.frag
)

set(PBR_SHADOW_SHADER_OUTPUTS)
foreach(SHADER_SOURCE IN LISTS PBR_SHADOW_SHADER_SOURCES)
    get_filename_component(SHADER_FILE_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Shaders/${SHADER_FILE_NAME}.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/Shaders
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compile GLSL shader ${SHADER_FILE_NAME}"
        VERBATIM
    )
    list(APPEND PBR_SHADOW_SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(PbrShadowExampleShaders DEPENDS ${PBR_SHADOW_SHADER_OUTPUTS})

add_executable(PbrShadowExample
    PbrShadowExample.cpp
    RenderDefinitions.hpp
    RenderVulkan.hpp
    RenderVulkan.cpp
)

add_dependencies(PbrShadowExample PbrShadowExampleShaders)

target_include_directories(PbrShadowExample PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(PbrShadowExample PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(PbrShadowExample PRIVATE /utf-8)
endif()

target_compile_definitions(PbrShadowExample PRIVATE
    PBR_SHADOW_EXAMPLE_SHADER_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/Shaders\"
)

target_link_libraries(PbrShadowExample PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

if(WIN32)
    add_library(RenderD3D11 STATIC
        RenderD3D11.hpp
        RenderD3D11.cpp
        RenderDefinitions.hpp
    )

    target_include_directories(RenderD3D11 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(RenderD3D11 PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options(RenderD3D11 PRIVATE /utf-8)
    endif()

    target_link_libraries(RenderD3D11 PUBLIC
        glm::glm
        d3d11
        dxgi
        dxguid
        d3dcompiler
    )

    add_executable(PbrShadowD3D11Example
        PbrShadowD3D11Example.cpp
        RenderDefinitions.hpp
    )

    target_include_directories(PbrShadowD3D11Example PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(PbrShadowD3D11Example PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(PbrShadowD3D11Example PRIVATE /utf-8)
    endif()

    target_compile_definitions(PbrShadowD3D11Example PRIVATE
        PBR_SHADOW_D3D11_SHADER_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Shaders\"
    )

    target_link_libraries(PbrShadowD3D11Example PRIVATE
        RenderD3D11
        glfw
        glm::glm
    )
endif()

if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    add_custom_command(TARGET PbrShadowExample POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<CONFIG>/bin/glfw3.dll"
            $<TARGET_FILE_DIR:PbrShadowExample>
    )

    if(TARGET PbrShadowD3D11Example)
        add_custom_command(TARGET PbrShadowD3D11Example POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<CONFIG>/bin/glfw3.dll"
                $<TARGET_FILE_DIR:PbrShadowD3D11Example>
        )
    endif()
endif()
