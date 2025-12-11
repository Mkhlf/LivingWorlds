# Living Worlds Task List

## Week 1: Vulkan Foundation
- [x] Initialize Git Repository and Workspace [x]
- [x] Review Reference Repository (bryanoliveira/cellular-automata) [x]
- [ ] Project Setup
    - [ ] Create CMake project structure
    - [ ] Add dependencies (Vulkan, GLFW, GLM, vk-bootstrap, VMA)
    - [ ] Create main.cpp entry point
- [ ] Vulkan Initialization (Boilerplate)
    - [ ] Instance creation
    - [ ] Physical device selection
    - [ ] Logical device creation
    - [ ] Queue families setup
    - [ ] Swapchain creation
- [ ] Graphics Pipeline Basics
    - [ ] Render pass
    - [ ] Framebuffers
    - [ ] Command buffers
    - [ ] Synchronization primitives (fences, semaphores)
- [ ] Compute Pipeline Basics
    - [ ] Compute shader loading module
    - [ ] Pipelie layout
    - [ ] Descriptor sets
- [ ] Verification
    - [ ] Clear screen test (Red screen)
    - [ ] Basic compute dispatch test

## Week 2: Basic 2D Cellular Automata
- [ ] CA Compute Shader
    - [ ] Implement Game of Life rules
    - [ ] Define input/output image bindings
- [ ] Host-Side Logic
    - [ ] Ping-pong buffering for CA state (Image A -> Image B)
    - [ ] Command recording for compute dispatch
    - [ ] Pipeline barriers for sync
- [ ] Rendering
    - [ ] Render CA output texture to screen quad
    - [ ] Handle resource transitions
- [ ] Interaction
    - [ ] Basic glider pattern initialization
- [ ] Validation
    - [ ] Verify 60 FPS performance on 1024x1024 grid

## Future Weeks (3-6)
- [ ] Week 3: Heightmap & Erosion
- [ ] Week 4: Biome Layer & Ecology
- [ ] Week 5: 2.5D Rendering
- [ ] Week 6: Interaction & Polish
