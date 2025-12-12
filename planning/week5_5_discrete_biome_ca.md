# Week 5.5: Discrete Biome CA Implementation Plan

## Goal Description
Transition from **continuous temp/hum diffusion** to **discrete 8-state biome CA** with neighbor-based spreading rules. This aligns with the original proposal and makes the ecological simulation visually distinct and configurable.

## Current State
- `biome_growth.comp` uses continuous `temp` and `hum` values (0.0-1.0).
- Biome type is **derived** at render time from height/temp/hum thresholds.
- No explicit biome state storage.

## Target State
- Explicit **8-state biome texture** (`R8_UINT`).
- Each cell stores a discrete biome ID (0-7).
- Spreading rules based on **neighbor count** (like Game of Life).
- Parameters (thresholds, probabilities) configurable via **Push Constants**.

---

## Proposed Changes

### 1. Data Structures

#### [NEW] Biome Image (`src/living_worlds.hpp`, `.cpp`)
```cpp
VkImage biome_images[2];         // Ping-pong discrete biome state
VmaAllocation biome_allocations[2];
VkImageView biome_views[2];
// Format: VK_FORMAT_R8_UINT (8-bit unsigned integer)
```

#### [MODIFY] Push Constants (`src/living_worlds.hpp`)
```cpp
struct BiomePushConstants {
    float forestSpreadChance;   // 0.0-1.0 (default 0.3)
    float desertSpreadChance;   // 0.0-1.0 (default 0.1)
    int   forestNeighborThreshold; // (default 3)
    int   desertNeighborThreshold; // (default 4)
    float time;                 // For random hash seed
};
```

---

### 2. Biome IDs

| ID | Biome   | Color (RGB)       | Spread Rule |
|----|---------|-------------------|-------------|
| 0  | Water   | (0.1, 0.3, 0.8)   | Height < 0.3 only |
| 1  | Sand    | (0.9, 0.8, 0.6)   | Adjacent to water |
| 2  | Grass   | (0.3, 0.6, 0.3)   | Default land |
| 3  | Forest  | (0.1, 0.4, 0.1)   | If 3+ forest neighbors → 30% chance |
| 4  | Desert  | (0.8, 0.7, 0.5)   | If 4+ desert neighbors → 10% chance |
| 5  | Rock    | (0.5, 0.5, 0.5)   | Height > 0.7 |
| 6  | Snow    | (0.95, 0.95, 1.0) | Height > 0.85 |
| 7  | Tundra  | (0.6, 0.7, 0.7)   | High altitude + cold |

---

### 3. Compute Shader

#### [NEW] `shaders/biome_ca.comp`
```glsl
#version 450
layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 2, rgba8) uniform readonly image2D heightMap;
layout(set = 0, binding = 8, r8ui) uniform readonly uimage2D inBiome;
layout(set = 0, binding = 9, r8ui) uniform writeonly uimage2D outBiome;

layout(push_constant) uniform PushConstants {
    float forestChance;
    float desertChance;
    int forestThreshold;
    int desertThreshold;
    float time;
} pc;

// Simple hash for pseudo-random
float hash(ivec2 p) {
    return fract(sin(dot(vec2(p), vec2(12.9898, 78.233)) + pc.time) * 43758.5453);
}

int countNeighbors(ivec2 pos, uint targetBiome, ivec2 size) {
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            ivec2 nPos = clamp(pos + ivec2(dx, dy), ivec2(0), size - 1);
            if (imageLoad(inBiome, nPos).r == targetBiome) count++;
        }
    }
    return count;
}

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outBiome);
    if (pos.x >= size.x || pos.y >= size.y) return;

    float h = imageLoad(heightMap, pos).r;
    uint current = imageLoad(inBiome, pos).r;
    uint newBiome = current;

    // Height Constraints (Hard Rules)
    if (h < 0.3) {
        newBiome = 0; // Water
    } else if (h > 0.85) {
        newBiome = 6; // Snow
    } else if (h > 0.7) {
        newBiome = 5; // Rock
    } else {
        // Ecological Spreading (Soft Rules)
        int forestCount = countNeighbors(pos, 3, size);
        int desertCount = countNeighbors(pos, 4, size);
        
        if (current == 2) { // Grass
            if (forestCount >= pc.forestThreshold && hash(pos) < pc.forestChance) {
                newBiome = 3; // → Forest
            } else if (desertCount >= pc.desertThreshold && hash(pos) < pc.desertChance) {
                newBiome = 4; // → Desert
            }
        }
        // Forest can't revert (stable)
        // Desert can spread but also be overtaken by forest
        if (current == 4 && forestCount >= 5) {
            newBiome = 3; // Reforestation
        }
    }

    imageStore(outBiome, pos, uvec4(newBiome));
}
```

---

### 4. Initialization

#### [MODIFY] `dispatch_biome_init()` (`src/living_worlds.cpp`)
Initialize all land cells to `Grass (2)`, water cells to `Water (0)` based on heightmap.

---

### 5. Rendering

#### [MODIFY] `shaders/terrain.frag`
Replace temp/hum sampling with discrete biome lookup:
```glsl
layout(set = 1, binding = 4) uniform usampler2D biomeMap; // R8_UINT

const vec3 biomeColors[8] = vec3[](
    vec3(0.1, 0.3, 0.8), // Water
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
```

---

### 6. Configurability

#### [MODIFY] `process_input()` (`src/living_worlds.cpp`)
Add keys to adjust parameters at runtime:
- **`F`**: Increase forest spread chance (+0.05)
- **`G`**: Decrease forest spread chance (-0.05)
- **`D`**: Toggle desert spread on/off

---

## Implementation Order
1. Create `biome_images` with `R8_UINT` format.
2. Update descriptor sets to bind biome images.
3. Create `biome_ca.comp` shader.
4. Create `init_biome_ca_pipeline()`.
5. Update `dispatch_biome_init()` to initialize discrete biomes.
6. Update `terrain.frag` to sample discrete biome.
7. Add push constants for configurability.
8. Verify forest spreading is visible.

---

## Verification Plan
- **Visual**: Watch forests spread from initial clusters.
- **Controls**: Adjust `forestChance` and see spreading speed change.
- **Metrics**: Print forest coverage % to console (optional).
