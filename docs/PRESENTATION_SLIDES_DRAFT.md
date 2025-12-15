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

<!--
Good morning everyone. I'm Mohammad, and today I'll be presenting Living Worlds - a GPU-accelerated terrain simulation engine I built for CS380.

As you can see from this animation, the system generates dynamic terrain in real-time, with different biomes spreading and evolving as the simulation runs.
-->

---

# The Problem

## Static Terrain is Boring

- Traditional approach: **Offline generation** or **manual sculpting**
- Games need: **Dynamic, evolving worlds**

### Our Goal
> Real-time terrain simulation using **GPU cellular automata**
> with **interactive framerates**

<!--
This project is about cellular automata - a classic concept in computer science where complex patterns emerge from simple local rules applied repeatedly over a grid.

The most famous example is Conway's Game of Life - cells live or die based on neighbor counts. But CA can model much more: spreading fires, crystal growth, and terrain evolution.

What I built is interactive world generation using CA rules on the GPU. It's not a physics simulation - we're not solving differential equations for water flow. Instead, simple rules like 'mass flows downhill if slope exceeds a threshold' create visually convincing terrain dynamics.
-->

---

# What We Built

## Living Worlds Engine

| Component | Description |
|-----------|-------------|
| **Geological Layer** | Thermal erosion simulation |
| **Ecological Layer** | 9 biome types with spreading rules |
| **Feedback Loop** | Forests stabilize terrain (80% resistance) |
| **Renderer** | 2.5D isometric with atmospheric fog |

<!--
Here's what Living Worlds actually does.

It has two main simulation layers. The geological layer handles thermal erosion - mass flowing downhill based on slope. The ecological layer manages 9 different biome types, each with their own spreading rules.

The key insight is the feedback loop between these layers. Forests actually stabilize the terrain - they reduce erosion by 80%. So where forests grow, mountains stay tall. Where deserts spread, erosion accelerates.

All of this is rendered in a 2.5D isometric view with atmospheric fog for depth.
-->

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

<!--
Let me explain the GPU architecture.

The core pattern is called ping-pong buffering. We maintain two copies of each data layer - heightmap A and heightmap B, biome A and biome B.

Each frame, we read from buffer A, compute the new state using our shaders, and write to buffer B. Then we swap. Next frame, we read from B, write to A.

Why is this essential? In cellular automata, every cell reads its neighbors. If we update in-place, some threads read old values while others read new values - that's a race condition. Ping-pong ensures all threads read the same consistent state.
-->

---

# Cellular Automata Rules

<div class="columns">
<div>

## Erosion (Diffusion)
*Each cell moves toward neighbor average*
```glsl
float neighborAvg = avgOf8Neighbors();
newH = h + (neighborAvg - h) * rate;
```

## Biome Feedback
*Biomes modify erosion rate*
```glsl
if (biome == FOREST) 
    rate *= forestMult; // UI: 0.2
if (biome == DESERT) 
    rate *= desertMult; // UI: 1.5
```

</div>
<div>

## Biome Spreading
*Height thresholds + neighbor counting*
```glsl
if (h < 0.30) biome = WATER;
if (h > 0.85) biome = SNOW;
if (forestNeighbors >= threshold)
    if (rand() < forestChance)
        biome = FOREST;
```

### Why Compute Shaders?
- **Parallel**: 9.4M threads
- **Local**: No global sync needed

</div>
</div>

<!--
Let me walk through the actual rules in the compute shaders.

For erosion, we calculate the average height of all 8 neighbors, then move each cell's height toward that average. The rate is controlled by a slider in the UI - 'Erosion Rate' adjusts how fast terrain smooths out.

The biome-erosion coupling modifies this rate based on biome type. In the UI, you see 'Forest Mult' - default 0.2 means forests reduce erosion to 20% of normal. 'Desert Mult' - default 1.5 means deserts erode 50% faster. There are also multipliers for sand and coastal areas.

For biome spreading, the rules are based on neighbor counting. Grass becomes forest if it has enough forest neighbors (controlled by 'Forest Threshold' in UI) and passes a probability check ('Forest Chance'). Same pattern for desert, wetland, and the mountain biomes.

Height also matters: below 0.30 is forced to water, above 0.85 is always snow. The 'Tree Line Height' parameter controls where forests transition to tundra.
-->

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

<!--
Here are the 9 biome types in the system.

Water is forced below a certain height. Sand appears in coastal zones. Grass is the default land cover and can convert to forest.

Forest is the key biome - it spreads to neighbors and resists erosion. Desert spreads in dry areas and accelerates erosion.

At high elevations we have rock, snow on peaks, tundra as a transition zone, and wetlands in low areas near water.
-->

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

<!--
Let's talk performance.

I benchmarked on an RTX 2080 across four grid sizes. At 512x512 - that's 262,000 cells - we hit over 3,000 FPS. At 1024x1024, about 1,400 FPS.

Even at 3072x3072 - that's 9.4 million vertices being simulated and rendered - we maintain 243 FPS. That's well above the 60 FPS target for interactive applications.

The key insight from this chart is that FPS scales linearly with grid area. Doubling the grid dimensions means roughly quartering the FPS, which is expected behavior for a GPU-bound workload.
-->

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

<!--
This chart shows what happens when we increase simulation speed.

At higher simulation speeds, we run more compute iterations per rendered frame. At 1000x speed, we see about a 30% FPS reduction.

But even at extreme simulation speeds, performance remains interactive. The system is well-optimized for the GPU's parallel architecture.
-->

---

# Demo Video

## Walkthrough

*Terrain generation, camera control, biome spawning, real-time CA parameter changes, and erosion dynamics.*

<!--
Let me show you a quick demo of the system in action.

[Play walkthrough_demo.mp4]

You can see terrain generation from noise, camera controls for navigation, spawning biomes with mouse clicks, and crucially - real-time parameter adjustment. I can change erosion rates, biome spreading probabilities, all while the simulation keeps running.
-->

---

# Future Work

- **Hydraulic erosion** - water flow simulation
- **Infinite terrain** - chunked streaming
- **Advanced lighting** - shadows, ambient occlusion

<!--
For future work, the main addition would be hydraulic erosion - simulating actual water flow and sediment transport, not just mass movement.

Infinite terrain using chunked streaming would allow much larger worlds.

And advanced lighting with shadows and ambient occlusion would significantly improve visual quality.
-->

---

# Thank You!

## Questions?
**Repository:**
[github.com/mkhlf/livingworlds](https://github.com/mkhlf/livingworlds)

![h:300](/home/mkhlf/Documents/cs380/livingworlds/benchmark_gifs/grid2048_speed10000.gif)

<!--
That's Living Worlds. The code is available on GitHub at this repository.

Thank you for listening. I'd be happy to take any questions.
-->
