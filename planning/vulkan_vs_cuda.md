# Tech Stack Decision: Vulkan vs. CUDA

## Executive Summary
For **Living Worlds**, **Vulkan** is the recommended choice over CUDA. 

While CUDA is a superior *pure compute* platform, your project is fundamentally a **real-time visual application** (2.5D rendering, lighting, interactive feedback). Vulkan provides a unified architecture where `Compute` (Simulation) and `Graphics` (Rendering) live in the same ecosystem, sharing memory resources directly. Using CUDA would require a complex "Split-Brain" architecture (CUDA for physics + OpenGL/Vulkan for rendering) which introduces synchronization overhead and development friction.

---

## Detailed Comparison

| Feature | Vulkan (Compute Shaders) | CUDA |
| :--- | :--- | :--- |
| **Primary Focus** | Graphics & Compute Integration | High-Performance Parallel Computing |
| **Vendor Support** | Cross-Platform (AMD, NVIDIA, Intel, Mac*) | NVIDIA Only (Vendor Lock-in) |
| **Rendering Integration** | **Native**. Compute writes to textures that shaders read immediately. | **Interop Required**. Must map/unmap resources to OpenGL/Vulkan. |
| **Language** | GLSL/HLSL (Shader languages) | C++ with extensions |
| **Development Friction** | High (Initial interactions), Low (Once pipeline is built) | Medium (Interop acts as a permanent "bridge" tax) |
| **Performance** | Excellent (Native GPU scheduling) | Best-in-class (for pure number crunching) |

### 1. The "Split-Brain" Problem (CUDA)
If you choose CUDA, your architecture becomes split:
1.  **Simulation (CUDA)**: Calculates `Heightmap[x][y]`.
2.  **Interop Bridge**: 
    *   Register OpenGL Buffer with CUDA.
    *   `cudaGraphicsMapResources()` (Locks buffer for CUDA).
    *   Kernel writes data.
    *   `cudaGraphicsUnmapResources()` (Unlocks for OpenGL).
3.  **Rendering (OpenGL/Vulkan)**: Draws the mesh.
*Downside*: This context switching limits how tightly you can couple physics and graphics.

### 2. The Unified Pipeline (Vulkan)
With Vulkan, everything is a command in a queue.
1.  **Compute Command**: `Dispatch(GameOfLifeShader)` → Writes to `VkImage`.
2.  **Memory Barrier**: `vkCmdPipelineBarrier` (Says "Wait for Compute to finish writing").
3.  **Draw Command**: `Draw(Terrain)` → Vertex Shader reads `VkImage` immediately.
*Upside*: Zero-copy, extremely low latency, perfect for 60 FPS simulations.

---

## What is Possible vs. Impossible?

### Vulkan
*   **Possible**: Everything in your scope (CA logic, millions of particles, 2.5D rendering).
*   **Difficulties**: 
    *   **Boilerplate**: Getting the first window and triangle requires ~500-800 lines of code (instance, device, swapchain, fences).
    *   **Debugging**: Shader debugging is harder than C++ debugging (typical `printf` is limited).
    *   **Synchronization**: You must manually tell the GPU when it's safe to read memory you just wrote.

### CUDA
*   **Possible**: Extremely complex physics models that might choke a shader (e.g., deep recursion, complex pointers).
*   **Not in Scope**: Rendering. CUDA cannot draw triangles, manage windows, or handle input.
*   **Difficulties**:
    *   **Environment**: Requires installing the proprietary CUDA Toolkit (GBs).
    *   **Distribution**: Your project will only run on NVIDIA cards.
    *   **Visuals**: You still need to learn 80% of OpenGL/Vulkan just to draw the results of your CUDA simulation.

---

## Project "CS380" Considerations
*   **Novelty**: The reference repo *already* uses CUDA+OpenGL. Doing the exact same thing is "re-implementing." Doing it in pure Vulkan demonstrates **modern game engine architecture**.
*   **Portfolio Value**: "I built a Vulkan Engine" is generally a stronger signal to game/graphics employers than "I hooked CUDA into OpenGL."

## Verdict for Week 1
**Stick with Vulkan.**
The initial pain of setup (Week 1) pays off by Week 3 when you are seamlessly eroding terrain in a compute shader and ray-marching it in a fragment shader without ever leaving the GPU context.
