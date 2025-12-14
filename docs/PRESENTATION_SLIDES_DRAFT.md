---
marp: true
theme: gaia
class: lead
paginate: true
backgroundColor: #f0f0f0
image: true
style: |
  section {
    font-size: 26px;
    padding: 40px;
  }
  h1 {
    color: #2c3e50;
    font-size: 1.8em;
  }
  h2 {
    color: #e67e22;
    font-size: 1.4em;
  }
  table {
    font-size: 20px;
    width: 100%;
  }
  code {
    background-color: #f8f9fa;
  }
  .columns {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 1rem;
  }
  a {
    color: #3498db;
    text-decoration: underline;
  }
---

# Living Worlds
## GPU-Accelerated Terrain Simulation
### CS380 Final Presentation

**Mohammad Alkhalifah**  
December 2025

![h:250](/home/mkhlf/Documents/cs380/livingworlds/benchmark_gifs/grid2048_speed10000.gif)

---

# The Problem

## Static Terrain is Boring

- Traditional approach: **Offline generation** or **manual sculpting**
- Games need: **Dynamic, evolving worlds**

### Our Goal
> Real-time terrain simulation using **GPU cellular automata**
> with **interactive framerates**

---

# What We Built

## Living Worlds Engine

| Component | Description |
|-----------|-------------|
| **Geological Layer** | Thermal erosion simulation |
| **Ecological Layer** | 9 biome types with spreading rules |
| **Feedback Loop** | Forests stabilize terrain (80% resistance) |
| **Renderer** | 2.5D isometric with atmospheric fog |

---

# GPU Architecture

<div class="columns">
<div>

## Data Flow

```
Heightmap A ──► Erosion ──► Heightmap B
    ↑                          │
    └────── Ping-Pong ─────────┘

Biome A ────► Biome CA ──► Biome B
    ↑                          │
    └────── Ping-Pong ─────────┘
```

</div>
<div>

### Why Ping-Pong?
- Prevents **race conditions** in parallel updates
- Each frame: Read A → Write B → Swap
- Essential for cellular automata correctness

</div>
</div>

---

# Cellular Automata Rules

<div class="columns">
<div>

## Thermal Erosion
*Physical Process*: mass flows downhill if slope > threshold
```glsl
if (h - neighborH > threshold) {
    // Transfer mass downhill
    newHeight = h - erosionRate;
}
```

## Feedback
*Coupling*: Biomes modify physical parameters
```glsl
if (biome == FOREST) 
    erosionRate *= 0.2; 
    // 80% reduction
```

</div>
<div>

## Biome Spreading
*Stochastic CA*: Random transitions based on neighbors
```glsl
if (isGrass && forestNeighbors >= 3) {
    if (random() < 0.3) 
        convertToList(FOREST);
}
```

### Why Compute Shaders?
- **Massively Parallel**: 3072² cells = 9.4M threads
- **Local Rules**: No global dependencies (perfect for GPU)

</div>
</div>

---

# Biome System

<!-- Slide 6: Reduced font size for table fit -->
<style scoped>
table { font-size: 18px; }
</style>

## 9 Discrete Biome Types

| Biome | Behavior |
|-------|----------|
| 🌊 Water | Height < 0.3 (forced) |
| 🏖️ Sand | Coastal zones |
| 🌱 Grass | Default land, converts to forest |
| 🌲 Forest | Spreads, resists erosion |
| 🏜️ Desert | Spreads in dry areas |
| 🪨 Rock | High elevation (>0.8) |
| ❄️ Snow | Peaks (>0.85) |
| 🏔️ Tundra | Alpine transition |
| 💧 Wetland | Low areas near water |

---

# Performance Results

## Benchmark: FPS vs Grid Size

<div class="columns">
<div>

![h:350](/home/mkhlf/Documents/cs380/livingworlds/benchmark_plots/fps_by_grid_size.png)

</div>
<div>

| Grid | Vertices | FPS |
|------|----------|-----|
| 512² | 262K | 3,062 |
| 1024² | 1.0M | 1,414 |
| 2048² | 4.2M | 505 |
| **3072²** | **9.4M** | **243** |

</div>
</div>

---

# Scalability Analysis

## FPS vs Simulation Speed

<div class="columns">
<div>

![h:300](/home/mkhlf/Documents/cs380/livingworlds/benchmark_plots/fps_by_speed.png)

</div>
<div>

### Key Findings
- FPS decreases linearly with compute load
- 1000× sim speed costs ~30% FPS
- Interactive performance maintained

</div>
</div>

---

# Demo Video

## Walkthrough

*Terrain generation, camera control, biome spawning, real-time CA parameter changes, and erosion dynamics.*

---

# Future Work

- **Hydraulic erosion** - water flow simulation
- **Infinite terrain** - chunked streaming
- **Advanced lighting** - shadows, ambient occlusion

---

# Thank You!

## Questions?
**Repository:**
[github.com/mkhlf/livingworlds](https://github.com/mkhlf/livingworlds)

![h:300](/home/mkhlf/Documents/cs380/livingworlds/benchmark_gifs/grid2048_speed10000.gif)
