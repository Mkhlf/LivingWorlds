# Week 3: Heightmap Integration & Erosion

## Objectives
1.  **Heightmap Data Structure**: Transition from the current 8-bit RGBA "Game of Life" grid to a high-precision floating point heightmap (`R32_SFLOAT`).
2.  **Noise Initialization**: Implement a compute shader to generate an initial fractal terrain (Perlin/Simplex noise) instead of the manual patterns.
3.  **Erosion Simulation**: Implement a compute shader that simulates simple thermal/hydraulic erosion to smooth the terrain over time.
4.  **Visualization**: Render the heightmap using a color gradient (e.g., Deep Blue -> Green -> White) to visualize elevation.

## Implementation Details

### 1. Data Structures
*   **New Images**: `heightmap_images[2]` (Ping-Pong).
*   **Format**: `VK_FORMAT_R32_SFLOAT`.
    *   *Reason*: 8-bit (256 values) is too "stepped" for smooth terrain. 32-bit float gives infinite continuous logic.
*   **Descriptor Sets**: Update set layout to bind `heightmap_images` as `readonly image2D` (input) and `writeonly image2D` (output).

### 2. Initialization (Noise)
Instead of CPU-side `stagingBuffer` (which is slow for complex noise), we will write a `noise_init.comp` shader.
*   **Algorithm**: Fractal Brownian Motion (fBm) using 4-6 octaves of value or simplex noise.
*   **Execution**: Run once at startup to populate `heightmap_images[0]`.

### 3. Erosion Compute Shader (`erosion.comp`)
This replaces `game_of_life.comp` for this week.
*   **Logic**:
    *   Read `height` at `center`.
    *   Read `height` of 8 neighbors.
    *   Calculate `average_difference` (slope).
    *   **Rules**:
        *   If `height >> neighbors` (Peak): Transfer mass to neighbors (Erode).
        *   If `height << neighbors` (Valley): Gain mass from neighbors (Deposit).
        *   *Simplified Thermal Erosion*: If `(height - neighbor) > talus_threshold`, move fraction of difference to neighbor.

### 4. Visualization (Render/Blit)
We can't just blit `R32` to the screen (it expects RGB).
*   **Visualization Shader**: `heightmap_viz.comp`.
    *   Input: `heightmap_image` (Current Output).
    *   Output: `swapchain_image`.
    *   Logic: Map `height` (-1.0 to 1.0) to Color Gradient.
        *   < 0.0: Blue (Water)
        *   0.0 - 0.2: Yellow (Sand)
        *   0.2 - 0.7: Green (Land)
        *   > 0.8: White (Snow)

## Testing Strategy
1.  **Noise Verification**: Run `noise_init.comp`. Result should look like "clouds" or "mountains", not random TV static.
    *   *Test*: Screenshot result.
2.  **Erosion Verification**: Initialize a "Spike" (single high pixel). Run erosion.
    *   *Expected*: The spike should spread out into a mound/hill over frames.
3.  **Stability**: Ensure the simulation doesn't explode (values going to Infinity).
    *   *Mitigation*: Clamp values in shader.

## Challenges & Risks
*   **Synchronization**: We are adding more passes (Init -> Erosion -> Viz).
    *   *Solution*: Strict barriers between each.
*   **Precision**: `R32` is good, but erosion logic can be unstable if time step is too large.
    *   *Solution*: Use small constants for erosion rate (e.g., 0.01).

## Tasks
- [ ] Refactor `LivingWorlds` to support `R32_SFLOAT` images.
- [ ] Create `shaders/noise_init.comp`.
- [ ] Create `shaders/erosion.comp`.
- [ ] Create `shaders/heightmap_viz.comp`.
- [ ] Update `run()` loop to execute: Erosion -> Barrier -> Viz -> Present.
