# Living Worlds
**Multi-Scale GPU-Accelerated Cellular Automata for Dynamic Terrain Generation**

## Overview
Living Worlds is a high-performance simulation engine that bridges geological and ecological processes using Vulkan compute shaders. Ideally running at 60 FPS on 1024x1024 grids, it demonstrates emergent complexity where forests stabilize terrain against erosion, created for CS380.

## Features (Planned)
- **Vulkan-based Engine**: Explicit control over GPU synchronization for maximum performance.
- **Dual-Layer CA**:
  - *Geological Layer*: Continuous heightmap evolution (erosion/deposition).
  - *Ecological Layer*: Discrete biome states (forest, desert, water) with spreading rules.
- **2.5D Rendering**: Isometric visualization with dynamic height displacement and lighting.
- **Interactive**: Real-time manipulation of simulation rules and parameters.

## Architecture
- **Language**: C++20
- **Graphics API**: Vulkan 1.3
- **Windowing**: GLFW
- **Math**: GLM

## Building
*Instructions to be added.*

## References
- [End-to-End Implementation Plan](End-to-End%20Implementation%20Plan.md)
- [Reference Repository Analysis](planning/reference_review.md)
