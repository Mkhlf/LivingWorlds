# Living Worlds: End-to-End Implementation Plan

## SECTION 1: PROJECT OVERVIEW

### 1.1 Executive Summary
Multi-scale GPU-accelerated cellular automata system for dynamic terrain generation using Vulkan compute shaders. The project couples geological CA (heightmap evolution), ecological CA (biome dynamics), and renders in 2.5D with isometric projection. This bridges parallel computing, complex systems theory, and real-time graphics in a unified application demonstrating emergent pattern formation at 60 FPS on 1024² grids.

**Key Innovation:** Bidirectional feedback loops between geological and ecological layers where forests stabilize terrain, creating realistic ecosystem evolution rather than static procedural generation.

**Why This Matters:** Demonstrates GPU programming across compute and graphics pipelines; creates foundation for infinite evolving game worlds; provides interactive visualization of emergence and complexity theory.

### 1.2 Visual Target
- **Reference Games:** Hades (vibrant colors, strong shadows), Hollow Knight (atmospheric depth), Cuphead (layered parallax)
- **2.5D Definition:** Isometric/angled orthographic projection with height-based vertex displacement, parallax scrolling, and atmospheric perspective to create depth illusion without full 3D complexity
- **Visual Techniques:** 
  - Height-based shading (valleys dark, peaks light with rim lighting)
  - Layered rendering with parallax (background 0.5×, terrain 1.0×, UI fixed)
  - Strong directional lighting and atmospheric fog for depth cues

### 1.3 Success Definition

**Minimum Viable Product (MVP):**
- [x] 2D Conway's Game of Life on GPU at 60 FPS (1024²)
- [ ] Basic fractal heightmap generation
- [ ] Simple rendering (flat-shaded acceptable)
- [ ] One user interaction (spawn pattern OR adjust rules)

**Target Deliverable:**
- [ ] Heightmap CA with erosion/deposition rules
- [ ] 6-8 biome types with ecological spreading
- [ ] Bidirectional feedback (forests ↔ terrain stability)
- [ ] 2.5D isometric rendering with lighting
- [ ] Interactive controls (rule editing, pattern spawning, time control)
- [ ] Performance: 60 FPS on 1024² grid

**Stretch Goals:**
- [ ] 3D volumetric CA (256³) for caves at 30 FPS
- [ ] Reaction-diffusion patterns for organic textures
- [ ] Time-lapse recording (1000× speed geological time)
- [ ] Advanced lighting (ambient occlusion, shadow casting)
- [ ] 2048² or 4096² grid support

---

## SECTION 2: TECHNICAL FOUNDATION

### 2.1 Vulkan Architecture Overview

**Why Vulkan over OpenGL:**
- Explicit synchronization control crucial for compute-graphics interop
- Lower CPU overhead enables higher framerates
- Modern API design matches GPU hardware model
- Better performance profiling via timestamp queries

**Key Vulkan Concepts to Master:**
- **Command Buffers:** Record GPU work, submit to queues
- **Descriptor Sets:** Bind textures/buffers to shaders (compute input/output images)
- **Pipeline Barriers:** Synchronize compute writes → graphics reads
- **Compute Pipelines:** CA update shaders separate from graphics
- **Memory Management:** VkImage vs VkBuffer trade-offs for CA state

**Resources from Research:**
- [Vulkan Tutorial](https://vulkan-tutorial.com) - Complete initialization guide
- [bryanoliveira/cellular-automata](https://github.com/bryanoliveira/cellular-automata) - Reference for CA optimization (729 gen/s on 13500² grid with CUDA/OpenGL)
- VulkanMemoryAllocator (VMA) library for simplified memory management

### 2.2 Cellular Automata Theory

**Conway's Game of Life Fundamentals:**
- Binary state: alive (1) or dead (0)
- Moore neighborhood: 8 surrounding cells
- Rules: Survive with 2-3 neighbors, birth with exactly 3
- Produces gliders, oscillators, stable patterns

**Multi-State CA Extensions:**
- **Heightmap CA:** Continuous values (elevation) instead of binary
- **Biome CA:** Discrete states (water, sand, grass, forest, desert, rock, snow, tundra)
- **Hybrid Rules:** Erosion/deposition based on height difference with neighbors

**Performance Considerations:**
- GPU advantage: Massively parallel (1024² = 1M cells updated simultaneously)
- Memory bandwidth critical: Coalesced texture reads essential
- Workgroup size tuning: Test 8×8, 16×16, 32×32 for occupancy

**Relevant Papers:**
- Jako & Toth (2011): "Interactive Hydraulic and Thermal Erosion on the GPU" - GPU Pro 2
- Chan (2020): "Lenia and Expanded Lenia: Continuous Cellular Automata" - Artificial Life Conference

### 2.3 2.5D Rendering Techniques

**Isometric Projection Mathematics:**
```cpp
// 30° isometric (Hades-style)
mat4 iso = rotate(30°, vec3(1,0,0)) * rotate(45°, vec3(0,1,0));
mat4 proj = ortho(-w/2, w/2, -h/2, h/2, near, far) * iso;
```

**Layered Rendering for Depth:**
- Background layer: Blurred, 0.5× parallax speed
- Terrain layer: Full detail, 1.0× speed, height displacement
- Foreground/UI: Fixed position, no parallax

**Height-Based Lighting:**
- Normal calculation: Sample neighbors, compute derivatives
- Diffuse lighting: `dot(normal, sunDir)`
- Ambient occlusion approximation: Average neighbor heights
- Atmospheric fog: `mix(color, fogColor, distance)`

### 2.4 Prior Work Analysis

| Work | Year | Key Technique | Relevance | Performance | Code |
|------|------|---------------|-----------|-------------|------|
| bryanoliveira/cellular-automata | 2021 | CUDA+OpenGL CA | Performance baseline | 729 gen/s (13500² dense) | [GitHub](https://github.com/bryanoliveira/cellular-automata) |
| Jako & Toth GPU Erosion | 2011 | Hydraulic/thermal GPU | Physics rules | Real-time on 2011 hardware | No |
| Losasso & Hoppe Clipmaps | 2004 | Terrain LOD | Rendering optimization | 60 FPS large terrains | Technique only |

**Gap Our Project Fills:**
- First Vulkan implementation of multi-scale CA terrain (most use OpenGL/CUDA)
- Couples geological and ecological CA with feedback (novelty)
- 2.5D rendering specifically for CA-generated worlds

---

## SECTION 3: ARCHITECTURE DESIGN

### 3.1 System Architecture

```
User Input (GLFW) → Camera/Rule Controls
         ↓
Application Layer (C++)
         ↓
Vulkan Abstraction Layer
         ↓↓↓
┌─────────────────┬──────────────────┬──────────────────┐
│ Compute Path    │ Graphics Path    │ Transfer Path    │
├─────────────────┼──────────────────┼──────────────────┤
│ Geological CA   │ Terrain Mesh     │ Host↔Device      │
│ Ecological CA   │ Fragment Shader  │ (Initial setup)  │
│ Noise Gen       │ (Biome colors)   │                  │
└─────────────────┴──────────────────┴──────────────────┘
```

### 3.2 Data Flow (Ping-Pong Pattern)

```
Frame N:
  Compute: Read HeightmapA → Write HeightmapB
          Read BiomeA → Write BiomeB
  Barrier: Sync compute→graphics
  Graphics: Read HeightmapB + BiomeB → Render Frame

Frame N+1:
  Compute: Read HeightmapB → Write HeightmapA
          Read BiomeB → Write BiomeA
  Barrier: Sync
  Graphics: Read HeightmapA + BiomeA → Render
```

### 3.3 Memory Layout

**Heightmap Texture:**
- Format: `VK_FORMAT_R32_SFLOAT` (4 bytes/pixel, -10.0 to +10.0 range)
- Size: 1024×1024 = 4MB per buffer × 2 (ping-pong) = 8MB
- Usage: `VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`

**Biome Texture:**
- Format: `VK_FORMAT_R8_UINT` (1 byte/pixel, 0-7 biome IDs)
- Size: 1024×1024 = 1MB per buffer × 2 = 2MB
- Usage: Same as heightmap

**Total GPU Memory:** ~10MB for CA state + ~50MB for rendering (vertex buffers, framebuffers) = ~60MB

**Decision Point:** VkImage (textures) chosen over VkBuffer (SSBO) for texture cache advantage on neighbor sampling

---

## SECTION 4: IMPLEMENTATION ROADMAP

### Week 1: Vulkan Foundation
**Goals:**
- [ ] Vulkan instance, device, queue creation
- [ ] GLFW window + Vulkan surface
- [ ] Compute pipeline setup (simple shader)
- [ ] Validation: Clear screen + basic compute dispatch

**Compute Shader (Test):**
```glsl
#version 450
layout(local_size_x = 16, local_size_y = 16) in;
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D outputImage;

void main() {
    imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), vec4(1,0,0,1));
}
```

**Deliverables:** Red screen rendered via compute shader proving pipeline works

**Risks:** ⚠️ Vulkan boilerplate ~500 lines before first pixel | **Mitigation:** Use vk-bootstrap library, follow Vulkan Tutorial exactly

**Resources:** [Vulkan Tutorial](https://vulkan-tutorial.com), VulkanMemoryAllocator, RenderDoc for debugging

**Questions:**
- Q: GLFW vs SDL2? → Decision: GLFW (lighter, Vulkan Tutorial uses it)
- Q: Validation layers in release? → Decision: Dev only, disable for final benchmarks

---

### Week 2: Basic 2D Cellular Automata
**Goals:**
- [ ] Conway's Game of Life in compute shader
- [ ] Ping-pong texture swap each frame
- [ ] Render CA as colored pixels (white=alive, black=dead)
- [ ] Spawn glider pattern, verify movement

**Compute Shader (Conway):**
```glsl
#version 450
layout(local_size_x = 16, local_size_y = 16) in;
layout(set = 0, binding = 0) uniform sampler2D inputState;
layout(set = 0, binding = 1, r8ui) uniform writeonly uimage2D outputState;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outputState);
    
    // Count alive neighbors (Moore, toroidal wrap)
    int alive = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            ivec2 neighbor = (pos + ivec2(dx, dy) + size) % size;
            alive += int(texelFetch(inputState, neighbor, 0).r > 0.5);
        }
    }
    
    // Conway rules
    uint current = uint(texelFetch(inputState, pos, 0).r > 0.5);
    uint newState = (alive == 3 || (alive == 2 && current == 1)) ? 1 : 0;
    
    imageStore(outputState, pos, uvec4(newState));
}
```

**Performance Target:** 1024² at 60 FPS (based on Bryan Oliveira achieving 729 gen/s on 13500²)

**Deliverables:** Video of glider moving across screen

**Risks:** ⚠️ Synchronization bugs (reading while writing) | **Mitigation:** Proper pipeline barriers, validation layers

---

### Week 3: Heightmap Integration + Erosion
**Goals:**
- [ ] Perlin/Simplex noise generation on GPU
- [ ] Initialize heightmap with fractal noise (fBm: 4-6 octaves)
- [ ] Hybrid CA rules: Erosion if `height > avg(neighbors) + threshold`
- [ ] Visualize: Height → color gradient (blue=low, white=high)

**Erosion CA Rules:**
```glsl
float avgHeight = 0.0;
for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
        if (dx == 0 && dy == 0) continue;
        avgHeight += sampleHeight(pos + ivec2(dx, dy));
    }
}
avgHeight /= 8.0;

float current = sampleHeight(pos);
float diff = current - avgHeight;
float erosionRate = 0.01;

if (diff > 0.1) { // Peak erodes
    newHeight = current - erosionRate;
} else if (diff < -0.1) { // Valley fills
    newHeight = current + erosionRate * 0.5;
} else {
    newHeight = current; // Stable
}
```

**Deliverables:** Time-lapse showing mountains smoothing over 1000 steps

**Risks:** ⚠️ Terrain becomes too flat | **Mitigation:** Tune erosion rate carefully (0.001-0.01 range), keep fractal baseline

---

### Week 4: Multi-State Biome Layer
**Goals:**
- [ ] 8 biome types: Water (0), Sand (1), Grass (2), Forest (3), Desert (4), Rock (5), Snow (6), Tundra (7)
- [ ] Elevation constraints: Water <0.3, Snow >0.85
- [ ] Ecological rules: Grass→Forest if 3+ forest neighbors (30% probability)
- [ ] Feedback: Forests reduce erosion rate in geological layer

**Biome Compute Shader:**
```glsl
uint biome = texelFetch(biomeInput, pos, 0).r;
float height = texelFetch(heightInput, pos, 0).r;

// Hard elevation constraints
if (height < 0.3) biome = WATER;
else if (height > 0.85) biome = SNOW;
else {
    // Ecological spreading
    int forestCount = countNeighbors(pos, FOREST);
    if (biome == GRASS && forestCount >= 3) {
        if (randomHash(pos) < 0.3) biome = FOREST;
    }
    // Add desert, rock, tundra rules...
}

imageStore(biomeOutput, pos, uvec4(biome));
```

**Coupling:** In erosion shader, multiply `erosionRate` by 0.5 if biome == FOREST

**Deliverables:** Screenshot showing distinct biome regions, video of forest spreading

---

### Week 5: 2.5D Rendering Pipeline
**Goals:**
- [ ] Isometric camera (30° angle)
- [ ] Vertex displacement by heightmap
- [ ] Normal mapping from height derivatives
- [ ] Biome-based fragment colors
- [ ] Atmospheric fog

**Vertex Shader:**
```glsl
layout(location = 0) in vec2 inGridPos; // (0,0) to (1024,1024)
layout(binding = 0) uniform sampler2D heightmap;

void main() {
    float height = texture(heightmap, inGridPos / 1024.0).r;
    vec3 worldPos = vec3(inGridPos.x, height * 10.0, inGridPos.y); // Scale height
    
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    outUV = inGridPos / 1024.0;
}
```

**Fragment Shader:**
```glsl
layout(binding = 1) uniform sampler2D biomeMap;
vec3 biomeColors[8] = vec3[](
    vec3(0.1, 0.3, 0.6), // Water
    vec3(0.9, 0.8, 0.6), // Sand
    vec3(0.3, 0.6, 0.3), // Grass
    vec3(0.1, 0.4, 0.1), // Forest
    vec3(0.8, 0.7, 0.5), // Desert
    vec3(0.5, 0.5, 0.5), // Rock
    vec3(0.95, 0.95, 1.0), // Snow
    vec3(0.6, 0.7, 0.7)  // Tundra
);

uint biome = texture(biomeMap, inUV).r;
vec3 color = biomeColors[biome];

// Lighting (normal from height derivatives)
vec3 normal = calculateNormal(inUV);
float diffuse = max(0, dot(normal, sunDir));
color *= (0.3 + 0.7 * diffuse); // Ambient + diffuse

// Fog
float dist = length(outWorldPos - cameraPos);
color = mix(color, vec3(0.7, 0.8, 0.9), smoothstep(50, 200, dist));

outColor = vec4(color, 1);
```

**Deliverables:** Rendered terrain with depth perception, camera controls

---

### Week 6: Polish + Interaction + Documentation
**Goals:**
- [ ] ImGui UI: Sliders for erosion rate, CA rules, time speed
- [ ] Mouse click: Spawn CA patterns (glider, block)
- [ ] Time controls: Pause, play, 1-1000× speed
- [ ] Performance overlay: FPS, frame time, generation count
- [ ] Final report writing (8-12 pages)
- [ ] Demo video recording (2-3 min)

**UI Features:**
- Rule sliders: Survival (2-3), Birth (3), Erosion rate (0-0.1)
- Preset buttons: "Forest expansion", "Desert formation", "Reset"
- Performance: Real-time FPS graph

**Deliverables:** Polished demo application, final report, presentation slides, demo video

---

## SECTION 5: PERFORMANCE & OPTIMIZATION

### 5.1 Performance Targets

**Minimum (Pass):** 1024² CA update <2ms, Full frame <33ms (30 FPS)
**Target (Good):** 1024² CA update <1ms, Full frame <16ms (60 FPS)
**Stretch (Excellent):** 2048² at 60 FPS

### 5.2 Optimization Techniques

**Workgroup Size Tuning:** Test 8×8, 16×16, 32×32 → Measure occupancy with Nsight
**Memory Access:** Coalesced reads via texture cache
**Texture Format:** Benchmark R32F vs R16F vs R16_UNORM for heightmap
**Barriers:** Minimal barriers, only where necessary

---

## SECTION 6: RISK ASSESSMENT

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Vulkan too complex | High | High | Use vk-bootstrap, allocate Week 1 |
| CA patterns boring | Medium | Medium | Research interesting rules, multiple presets |
| Performance <30 FPS | Medium | High | Profile early, reduce grid if needed |
| Synchronization bugs | High | High | Validation layers, conservative barriers |
| 2.5D looks flat | Medium | Medium | Exaggerate height ×10, strong lighting |

---

## SECTION 7: DELIVERABLES CHECKLIST

- [ ] Source code (GitHub repo with README)
- [ ] Build instructions (CMake)
- [ ] Final report (8-12 pages)
- [ ] Demo video (2-3 min)
- [ ] Presentation slides (15-20 slides)
- [ ] Live demo (rehearsed)

---

## LIVING DOCUMENT STRUCTURE

This plan will be updated weekly with:
- **Weekly Development Logs** (progress, problems, decisions)
- **Performance Data** (as measured)
- **Lessons Learned** (what worked/didn't)
- **Code Snippets** (actual implementations)

**Next Update:** End of Week 1 with Vulkan setup results
