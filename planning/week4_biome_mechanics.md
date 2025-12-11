# Week 4: Multi-State Biome Layer & Ecological Feedback

## Objectives
1.  **Biome Data Structure**: Introduce a secondary state layer (`R8_UINT`) to represent distinct biome types (Water, Sand, Grass, Forest, Rock, Snow).
2.  **Ecological Constraints**: Initialize biomes based on **Height** (e.g., Water below 0.0, Snow above 0.8).
3.  **Ecological Simulation**: Implement a compute shader for biome evolution (e.g., Forest spreading into Grass).
4.  **Bidirectional Coupling**: Implement feedback where the Biome layer affects the Height layer (e.g., Forests reduce erosion rate).
5.  **Composite Rendering**: Render the final terrain using both Height (geometry/normals) and Biome (albedo color).

## Implementation Details

### 1. Data Structures
*   **New Images**: `biome_images[2]` (Ping-Pong).
*   **Format**: `VK_FORMAT_R8_UINT`.
    *   *Values*: 0=Water, 1=Sand, 2=Grass, 3=Forest, 4=Rock, 5=Snow.
*   **Descriptor Sets**:
    *   Compute Set: Bind `height_images` (Read) and `biome_images` (Read/Write). Be careful with circular dependencies if concurrent.
        *   Safe approach: Update Height -> Barrier -> Update Biome -> Barrier.
        *   Or: Height depends on Old Biome. Biome depends on Old Height. Both read "Old", write "New".

### 2. Biome Compute Shader (`biome_growth.comp`)
*   **Input**: `height_image` (Read), `biome_image` (Read Old).
*   **Output**: `biome_image` (Write New).
*   **Rules**:
    *   *Survival*: If `height < WATER_LEVEL`, force `biome = WATER`.
    *   *Growth*: If `biome == GRASS` and `neighbors(FOREST) >= 3` -> chance to become `FOREST`.
    *   *Death*: If `biome == FOREST` and `random() < 0.001` -> become `GRASS` (Fire/Death).

### 3. Feedback: Erosion Modification
*   **Update**: Modify `erosion.comp`.
*   **Input**: Add `biome_image` (Read).
*   **Logic**:
    *   Read `biome` at `pos`.
    *   `erosion_factor = 1.0`.
    *   If `biome == FOREST`, `erosion_factor = 0.1` (Roots hold soil).
    *   If `biome == ROCK`, `erosion_factor = 0.05` (Hard material).
    *   Apply `erosion_factor` to the mass transfer amount.

### 4. Visualization
*   **Update**: `heightmap_viz.comp` (or move to a true Graphics RenderPipeline?).
*   **Logic**:
    *   Read `height` -> Compute Normal (for lighting).
    *   Read `biome` -> Look up Color (`vec3 color_table[6]`).
    *   `FinalColor = BiomeColor * Lighting`.

## Testing Strategy
1.  **Biome Initialization**: Verify Water appears in valleys and Snow on peaks.
2.  **Growth Verification**: Seed a small forest. Watch it spread over time.
3.  **Feedback Verification**:
    *   Create a "hill" with Forest on one side, barren on the other.
    *   Run Erosion.
    *   *Expected*: The barren side erodes faster than the forested side.

## Challenges & Risks
*   **Complexity**: Two interacting CA layers doubles the state management.
    *   *Solution*: Keep the loop strict: `Erode(Height, OldBiome)` -> `Barrier` -> `Grow(Biome, NewHeight)`.
*   **Color Clashing**: Pure colors look bad.
    *   *Solution*: Blend colors at boundaries or use noise to vary the biome thresholds slightly.

## Tasks
- [ ] Add `biome_images` to `LivingWorlds`.
- [ ] Create `shaders/biome_growth.comp`.
- [ ] Update `shaders/erosion.comp` to read biome data.
- [ ] Update visualization to use Biome Colors instead of just Height Gradient.
