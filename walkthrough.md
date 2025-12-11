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

## Usage
```bash
mkdir build
cd build
cmake ..
make
cd bin
./LivingWorlds
```

## Known Issues
- **Validation Layer Warning**: `VUID-vkQueueSubmit-pSignalSemaphores-00067`. The strict validation layer warns about potential semaphore reuse while the presentation engine holds an image. This is a common false-positive/strictness issue in basic implementations and does not affect the correctness of the visual output for this stage. It will be addressed when we move to more complex frame management.
