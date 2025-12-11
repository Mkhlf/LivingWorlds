# Week 1 Walkthrough: Vulkan Foundation & Compute Verification

## Overview
We have successfully established the technical foundation for "Living Worlds". The application now initializes a full Vulkan 1.3 context, manages memory via VMA, and executes a compute pipeline that writes to a storage texture, which is then blitted to the screen.

## Achievements
- [x] **Project Structure**: CMake build system with `vk-bootstrap`, `VMA`, `GLFW`, and `GLM` dependencies fetching automatically.
- [x] **Vulkan Initialization**: Boilerplate reduction using `vk-bootstrap` (Instance, Device, Swapchain).
- [x] **Graphics/Compute Interop**: 
    - Created a **Storage Image** (`VK_IMAGE_USAGE_STORAGE_BIT`) for compute writes.
    - Implemented a **Compute Pipeline** that writes a solid green color.
    - Implemented a **Blit Command** to copy the compute result to the Swapchain for presentation.
- [x] **Synchronization**: Implemented "Frames in Flight" (double buffering) logic to allow CPU/GPU parallelism.

## Verification
The application runs and displays a **Green Screen**.
- **Why Green?** The RenderPass clears the screen to Red. The Compute Shader writes Green. Seeing Green confirms the Compute Shader is running and the Blit is working.

# Week 2 Walkthrough: Basic 2D Cellular Automata

## Overview
We built upon the Vulkan foundation to implement Conway's Game of Life. This involved creating a sophisticated double-buffered (ping-pong) state management system on the GPU to allow the simulation to evolve frame-by-frame.

## Achievements
- [x] **Game of Life Shader**: implemented `shaders/game_of_life.comp` with Conway's rules (Underpopulation, Survival, Overpopulation, Reproduction) using Moore neighborhood and toroidal wrapping.
- [x] **Ping-Pong Architecture**:
    - Created TWO storage images (`Image A` and `Image B`).
    - Implemented Double Buffering logic:
        - Frame N: Read A -> Write B
        - Frame N+1: Read B -> Write A
    - Managing Descriptor Sets to swap Input/Output bindings dynamically.
- [x] **State Initialization**: 
    - Implemented flexible initialization logic supporting multiple patterns:
        - **Glider**: Classic simple moving pattern.
        - **Gosper Glider Gun**: Complex pattern that generates gliders.
        - **Random**: 50% noise density.
    - Used a Staging Buffer to upload the initial pattern.
- [x] **Synchronization**:
    - Added Pipeline Barriers to strictly order Compute (Write) -> Compute (Read) dependencies between frames.
- [x] **Verification**:
    - Added frame counter to console output.
    - Verified Gosper Glider Gun pattern running at >60 FPS (1400+ frames verified).

## Verification
- **Visuals**: The application displays a Glider pattern moving across the screen (white cells on black background/initially cleared).
- **Performance**: The simulation runs on a 1024x1024 grid fully on the GPU.

## Usage
```bash
./LivingWorlds
```

## Known Issues
- **Validation Layer Warning**: `VUID-vkQueueSubmit-pSignalSemaphores-00067`. Use `VK_KHR_swapchain_maintenance1` to resolve strict semaphore reuse validation in future.
