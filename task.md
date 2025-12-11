# Living Worlds Task List

## Week 1: Vulkan Foundation
- [x] Initialize Git Repository and Workspace [x]
- [x] Review Reference Repository (bryanoliveira/cellular-automata) [x]
- [x] Project Setup
    - [x] Create CMake project structure
    - [x] Add dependencies (Vulkan, GLFW, GLM, vk-bootstrap, VMA)
    - [x] Create main.cpp entry point
- [x] Vulkan Initialization (Boilerplate)
    - [x] Instance creation
    - [x] Physical device selection
    - [x] Logical device creation
    - [x] Queue families setup
    - [x] Swapchain creation
- [x] Graphics Pipeline Basics
    - [x] Render pass
    - [x] Framebuffers
    - [x] Command buffers
    - [x] Synchronization primitives (fences, semaphores)
- [x] Compute Pipeline Basics
    - [x] Compute shader loading module
    - [x] Pipelie layout
    - [x] Descriptor sets
- [x] Verification
    - [x] Clear screen test (Red screen)
    - [x] Basic compute dispatch test

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
