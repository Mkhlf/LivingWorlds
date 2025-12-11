# Week 6: Interaction & Polish Implementation Plan

## Goal Description
The final week focuses on **User Interaction** and **Simulation Polish**. We will allow the user to interact with the world (paint terrain, change weather) and refine the visuals/simulation for a complete "Living World" experience.

## Proposed Changes

### 1. Interaction (Mouse & Keyboard)
*   **Raycasting**: Implement ray-plane intersection to determine which terrain cell the mouse is hovering over in 3D/2.5D view.
*   **Brush Tools**:
    *   **Raise/Lower Landscape**: Click and drag to modify the `heightmap_image`.
    *   **Paint Biomes**: Modify `temp_image` or `humidity_image` locally (e.g., make an area a desert).
*   **UI Overlay**: (Optional) Add simple ImGui overlay or keyboard shortcuts to switch tools.

### 2. Advanced Simulation Features
*   **Water Flow**: Simulate simple water movement downhill (hydraulic erosion particle system?).
*   **Seasonal Cycles**: Global uniform for `Time` that modulates temperature/growth (Winter/Summer).
*   **Atmosphere**: Simple fog or cloud layer.

### 3. Final Polish
*   **Post-Processing**: Add Bloom or Color Correction?
*   **Code Cleanup**: Refactor simplified "MVP" code into robust classes.

## Verification Plan
*   **Interaction Test**: Verify mouse clicks correctly affect the world at the cursor location.
*   **Stress Test**: Ensure "God Mode" painting doesn't crash the simulation.
