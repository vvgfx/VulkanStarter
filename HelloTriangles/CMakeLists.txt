cmake_minimum_required(VERSION 3.20)
project(VulkanApp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g")

# Find Vulkan
find_package(Vulkan REQUIRED)

# Find slangc executable
find_program(SLANGC_EXECUTABLE slangc.exe)

# Include directories
include_directories(
    ${Vulkan_INCLUDE_DIRS}
    ../include/GLFW
    ../include/glm
    ../include
)

# Library directories
link_directories(
    ${Vulkan_LIBRARY_DIRS}
    ../lib/glfw/lib-mingw-w64
)

# Shader compilation
set(SHADERS_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(SHADER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shader.slang)
set(SHADER_OUTPUT ${SHADERS_DIR}/slang.spv)

file(MAKE_DIRECTORY ${SHADERS_DIR})

add_custom_command(
    OUTPUT ${SHADER_OUTPUT}
    COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} 
        -target spirv -profile spirv_1_4 -emit-spirv-directly
        -fvk-use-entrypoint-name -entry vertMain -entry fragMain 
        -o ${SHADER_OUTPUT}
    DEPENDS ${SHADER_SOURCES}
    COMMENT "Compiling Slang shaders"
    VERBATIM
)

add_custom_target(shaders DEPENDS ${SHADER_OUTPUT})

# Main executable
add_executable(VulkanApp main.cpp)

# Precompiled header
target_precompile_headers(VulkanApp PRIVATE ../include/vulkan_pch.h)
add_dependencies(VulkanApp shaders)

# Link libraries
target_link_libraries(VulkanApp 
    glfw3
    Vulkan::Vulkan
    gdi32
    imm32
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)