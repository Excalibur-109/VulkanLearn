find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin"
)

if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "PbrShadowExample 需要 glslangValidator，请确认 Vulkan SDK 已安装并设置 VULKAN_SDK。")
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../RenderDefinitions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../RenderVulkan.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../RenderVulkan.cpp
)

add_dependencies(PbrShadowExample PbrShadowExampleShaders)

target_include_directories(PbrShadowExample PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(PbrShadowExample PRIVATE
    PBR_SHADOW_EXAMPLE_SHADER_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/Shaders\"
)

target_link_libraries(PbrShadowExample PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    add_custom_command(TARGET PbrShadowExample POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<CONFIG>/bin/glfw3.dll"
            $<TARGET_FILE_DIR:PbrShadowExample>
    )
endif()
