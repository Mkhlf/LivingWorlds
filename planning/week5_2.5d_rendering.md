# Week 5: 2.5D Rendering Implementation Plan

## Goal Description
Transition from the current 2D top-down visualization to a **2.5D Isometric/Perspective Renderer**. This will allow the user to visualize the terrain height more intuitively, creating a "SimCity-like" view of the living world. We will achieve this by rendering a 3D mesh (grid of vertices) displaced by the heightmap.

## User Review Required
> [!IMPORTANT]
> This requires a significant change to the rendering pipeline. We are moving from a single full-screen quad (2 triangles) to a large grid mesh (e.g., 1024x1024 vertices). Performance optimization (indexing, culling) might be needed.

## Proposed Changes

### 1. Resources & Vertex Data (`src/living_worlds.hpp`, `src/living_worlds.cpp`)
*   **Vertex Buffer**: Generate a flat grid of vertices (X, Z coordinates).
*   **Index Buffer**: Generate indices to stitch vertices into triangles.
*   **Uniform Buffer (UBO)**: Store MVP (Model-View-Projection) matrices to control the camera.
*   **Camera Class**: Implement a simple camera (orbit or fly) to navigate the 3D view.

### 2. Graphics Pipeline
*   **Update Render Pass**: Add distinct Depth Attachment (essential for 3D).
*   **New Graphics Pipeline**:
    *   **Vertex Shader**: Reads (X, Z) from buffer, reads Height from `heightmap_image` (Vertex Texture Fetch), displaces Y position.
    *   **Fragment Shader**: Similar to current `heightmap_viz.comp` logic but calculating lighting in 3D space. Read Biome data for color.

### 3. Shaders
#### [NEW] `shaders/terrain.vert`
*   Input: Grid Position (X, Z)
*   Sampler: Heightmap
*   Uniform: MVP Matrix
*   Logic: `gl_Position = Projection * View * Model * vec4(x, height * scale, z, 1.0);`

#### [NEW] `shaders/terrain.frag`
*   Input: Interpolated UVs, World Position, Normal (computed in VS or FS)
*   Sampler: Heightmap, Temp, Humidity
*   Logic: Texturing/Coloring based on Biome rules + Phong Lighting.

## Verification Plan
### Automated Tests
*   **Build Verification**: Ensure new shaders compile.

### Manual Verification
*   **Visual Check**:
    *   Confirm terrain looks 3D.
    *   Test camera movement (Rotate/Zoom).
    *   Verify performance is acceptable with full mesh rendering.
