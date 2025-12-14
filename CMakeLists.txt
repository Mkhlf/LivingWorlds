cmake_minimum_required(VERSION 3.20)
project(LivingWorlds LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# FetchContent for libraries
include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# vk-bootstrap: Simplifies Vulkan initialization
FetchContent_Declare(
    vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(vk-bootstrap)

# VulkanMemoryAllocator: Simplifies memory management
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)
# VMA is header only but provides a cmake target if we want, or we can just include directories.
# VMA's CMake support might be varying, usually people acts as flexible.
# Let's check if VMA creates a target, otherwise we add include directories manually.

# Shader Compilation
find_program(GLSLC_EXECUTABLE glslc HINTS ${Vulkan_SDK}/bin)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Please check your Vulkan SDK installation.")
endif()

set(SHADERS shaders/test.comp shaders/game_of_life.comp
    shaders/noise_init.comp
    shaders/heightmap_viz.comp
    shaders/erosion.comp
    shaders/biome_init.comp
    shaders/biome_growth.comp
    shaders/biome_ca.comp
    shaders/terrain.vert
    shaders/terrain.frag
)
set(SPV_SHADERS "")

file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders)

foreach(SHADER ${SHADERS})
    get_filename_component(FILENAME ${SHADER} NAME)
    set(SPV_FILE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${FILENAME}.spv)
    
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/${SHADER} -o ${SPV_FILE}
        DEPENDS ${CMAKE_SOURCE_DIR}/${SHADER}
        COMMENT "Compiling ${FILENAME} to SPIR-V"
    )
    list(APPEND SPV_SHADERS ${SPV_FILE})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SPV_SHADERS})

# Dear ImGui
add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC 
    external/imgui
    external/imgui/backends
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)

add_executable(LivingWorlds src/main.cpp src/living_worlds.cpp src/vma_impl.cpp)
add_dependencies(LivingWorlds Shaders)

target_link_libraries(LivingWorlds PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    vk-bootstrap
    VulkanMemoryAllocator
    imgui
)

target_include_directories(LivingWorlds PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Copy shaders to bin directory (if we had any yet)
# file(COPY shaders DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
