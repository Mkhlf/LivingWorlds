# Week 6: Polish & Interaction - Implementation Plan

## Overview
This document provides detailed implementation steps for completing the remaining Week 6 features. Each section is self-contained and can be implemented independently.

---

## 6.1 Bidirectional Feedback Loop

**Goal**: Forests stabilize terrain by reducing erosion rate.

**Time Estimate**: 30 minutes

### Implementation Steps

#### Step 1: Update Compute Descriptor Layout
Add biome image binding to erosion shader's descriptor set.

**File**: `src/living_worlds.cpp` - `create_compute_descriptors()`

```cpp
// Add binding 10 for biome read
VkDescriptorSetLayoutBinding biomeReadBinding{};
biomeReadBinding.binding = 10;
biomeReadBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
biomeReadBinding.descriptorCount = 1;
biomeReadBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
```

#### Step 2: Update Descriptor Writes
Bind biome image to the new binding.

```cpp
// In create_compute_descriptors(), add to Set 0:
VkDescriptorImageInfo biomeRead{VK_NULL_HANDLE, biome_views[0], VK_IMAGE_LAYOUT_GENERAL};
add_write(compute_descriptor_sets[0], 10, &biomeRead);

// Set 1:
VkDescriptorImageInfo biomeRead1{VK_NULL_HANDLE, biome_views[1], VK_IMAGE_LAYOUT_GENERAL};
add_write(compute_descriptor_sets[1], 10, &biomeRead1);
```

#### Step 3: Update Erosion Shader

**File**: `shaders/erosion.comp`

```glsl
// Add binding
layout(set = 0, binding = 10, r8ui) uniform readonly uimage2D biomeMap;

// In main(), after calculating erosionRate:
uint biome = imageLoad(biomeMap, pos).r;
if (biome == 3) { // FOREST
    erosionRate *= 0.2; // 80% reduction
}
```

#### Verification
- Watch forests over time
- Terrain under forests should erode slower than bare grass

---

## 6.2 VMA Memory Leak Fix

**Goal**: Clean shutdown without VMA assertion failure.

**Time Estimate**: 1 hour

### Diagnosis

The error: `Some allocations were not freed before destruction of this memory block!`

### Implementation Steps

#### Step 1: Audit cleanup() Function

**File**: `src/living_worlds.cpp` - `cleanup()`

Check for missing destroys (in reverse creation order):
```cpp
// Images to free:
// - biome_images[0,1] + biome_allocations[0,1]
// - temp_images[0,1] + temp_allocations[0,1]  
// - humidity_images[0,1] + humidity_allocations[0,1]
// - heightmap_images[0,1] + heightmap_allocations[0,1]
// - gol_images[0,1] + gol_allocations[0,1]
```

#### Step 2: Add Missing Frees

```cpp
// Free biome images
for (int i = 0; i < 2; i++) {
    vkDestroyImageView(device.device, biome_views[i], nullptr);
    vmaDestroyImage(allocator, biome_images[i], biome_allocations[i]);
}
// Repeat for temp_, humidity_ if still used
```

#### Step 3: Order Check
Ensure allocator is destroyed LAST, after all images:
```cpp
// ... destroy all images first ...
vmaDestroyAllocator(allocator); // Must be last
```

#### Verification
- Run application, close window
- Should terminate cleanly without abort

---

## 6.3 Atmospheric Fog

**Goal**: Add depth perception via distance-based fog.

**Time Estimate**: 15 minutes

### Implementation Steps

**File**: `shaders/terrain.frag`

```glsl
// At end of main(), before outColor:

// Camera position passed via UBO
vec3 cameraPos = ubo.cameraPos; // Add to UBO struct
float dist = length(inWorldPos - cameraPos);

// Fog parameters
vec3 fogColor = vec3(0.7, 0.8, 0.9); // Sky blue
float fogStart = 500.0;
float fogEnd = 1500.0;
float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);

finalColor = mix(finalColor, fogColor, fogFactor);
```

**File**: `src/living_worlds.hpp` - Add to UBO:
```cpp
struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    float time;
    int vizMode;
    glm::vec3 cameraPos; // NEW
};
```

---

## 6.4 Performance Overlay

**Goal**: Display FPS and frame timing.

**Time Estimate**: 1 hour (simple) / 2+ hours (ImGui)

### Simple Approach: Window Title

**File**: `src/living_worlds.cpp` - `draw()`

```cpp
// Existing FPS calculation already done, just update title
static double lastTitleUpdate = 0;
if (glfwGetTime() - lastTitleUpdate > 0.5) {
    char title[128];
    snprintf(title, sizeof(title), "Living Worlds | FPS: %d | Frame: %.2fms", 
             fps, 1000.0f / fps);
    glfwSetWindowTitle(window, title);
    lastTitleUpdate = glfwGetTime();
}
```

---

## 6.5 ImGui Integration

**Goal**: Parameter sliders and UI controls.

**Time Estimate**: 2-3 hours

### Implementation Steps

#### Step 1: Add Dependency

**File**: `CMakeLists.txt`
```cmake
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90
)
FetchContent_MakeAvailable(imgui)
```

#### Step 2: Initialize ImGui

**File**: `src/living_worlds.cpp` - `init_vulkan()`

```cpp
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

// After Vulkan init:
IMGUI_CHECKVERSION();
ImGui::CreateContext();
ImGui_ImplGlfw_InitForVulkan(window, true);

ImGui_ImplVulkan_InitInfo init_info = {};
init_info.Instance = instance;
init_info.PhysicalDevice = physical_device;
init_info.Device = device.device;
// ... fill remaining fields
ImGui_ImplVulkan_Init(&init_info);
```

#### Step 3: Render UI

**File**: `src/living_worlds.cpp` - `draw()`

```cpp
ImGui_ImplVulkan_NewFrame();
ImGui_ImplGlfw_NewFrame();
ImGui::NewFrame();

ImGui::Begin("Living Worlds Controls");
ImGui::SliderFloat("Erosion Rate", &erosionRate, 0.0f, 0.1f);
ImGui::SliderFloat("Forest Spread", &biomePushConstants.forestChance, 0.0f, 1.0f);
ImGui::SliderFloat("Simulation Speed", &simInterval, 0.001f, 0.5f);
if (ImGui::Button("Reset")) { /* reset logic */ }
ImGui::End();

ImGui::Render();
ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
```

---

## 6.6 Pattern Spawning

**Goal**: Click to spawn biome clusters.

**Time Estimate**: 1 hour

### Implementation Steps

#### Step 1: Mouse Picking

**File**: `src/living_worlds.cpp` - `handle_input()`

```cpp
if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    
    // Convert screen to world (simplified for overhead view)
    // More complex for perspective - use ray casting
    int gridX = (int)(xpos / screenWidth * width);
    int gridY = (int)(ypos / screenHeight * height);
    
    spawnBiomePatch(gridX, gridY, FOREST, 10); // 10 radius
}
```

#### Step 2: Implement Spawn Function

```cpp
void LivingWorlds::spawnBiomePatch(int cx, int cy, uint8_t biome, int radius) {
    // This requires staging buffer approach:
    // 1. Map biome image memory
    // 2. Write biome values in radius
    // 3. Unmap and sync
    
    // OR simpler: set a "spawn pending" flag and handle in compute
}
```

---

## Verification Checklist

| Feature | Test |
|---------|------|
| Bidirectional Feedback | Forests erode slower than grass |
| VMA Fix | Clean exit, no assert |
| Fog | Distant terrain fades to blue |
| FPS Overlay | Title shows current FPS |
| ImGui | Sliders adjust simulation |
| Spawning | Click creates biome patch |

---

## Priority Order

1. **Bidirectional Feedback** - Core feature, 30 min
2. **VMA Fix** - Bug fix, 1 hr
3. **Fog** - Low effort, high visual impact, 15 min
4. **FPS Overlay** - Simple, 15 min
5. **ImGui** - Nice to have, 2-3 hrs
6. **Spawning** - Optional, 1 hr
