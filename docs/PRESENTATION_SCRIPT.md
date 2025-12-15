# Living Worlds - Presentation Script

## Slide 1: Title (30 seconds)

> "Good morning everyone. I'm Mohammad, and today I'll be presenting Living Worlds - a GPU-accelerated terrain simulation engine I built for CS380.

> As you can see from this animation, the system generates dynamic terrain in real-time, with different biomes spreading and evolving as the simulation runs."

---

## Slide 2: The Problem (45 seconds)

> "This project is about **cellular automata** - a classic concept in computer science where complex patterns emerge from simple local rules applied repeatedly over a grid.

> The most famous example is Conway's Game of Life - cells live or die based on neighbor counts. But CA can model much more: spreading fires, crystal growth, and terrain evolution.

> What I built is **interactive world generation** using CA rules on the GPU. It's not a physics simulation - we're not solving differential equations for water flow. Instead, simple rules like 'mass flows downhill if slope exceeds a threshold' create visually convincing terrain dynamics.

---

## Slide 3: What We Built (45 seconds)

> "Here's what Living Worlds actually does.

> It has two main simulation layers. The **geological layer** handles thermal erosion - mass flowing downhill based on slope. The **ecological layer** manages 9 different biome types, each with their own spreading rules.

> The key insight is the **feedback loop** between these layers. Forests actually stabilize the terrain - they reduce erosion by 80%. So where forests grow, mountains stay tall. Where deserts spread, erosion accelerates.

> All of this is rendered in a 2.5D isometric view with atmospheric fog for depth."

---

## Slide 4: GPU Architecture (45 seconds)

> "Let me explain the GPU architecture.

> The core pattern is called **ping-pong buffering**. We maintain two copies of each data layer - heightmap A and heightmap B, biome A and biome B.

> Each frame, we read from buffer A, compute the new state using our shaders, and write to buffer B. Then we swap. Next frame, we read from B, write to A.

> **Why is this essential?** In cellular automata, every cell reads its neighbors. If we update in-place, some threads read old values while others read new values - that's a race condition. Ping-pong ensures all threads read the same consistent state."

---

## Slide 5: Cellular Automata Rules (60 seconds)

> "Let me walk through the actual rules in the compute shaders.

> For **erosion**, we calculate the average height of all 8 neighbors, then move each cell's height toward that average. The rate is controlled by a slider in the UI - 'Erosion Rate' adjusts how fast terrain smooths out.

> The **biome-erosion coupling** modifies this rate based on biome type. In the UI, you see 'Forest Mult' - default 0.2 means forests reduce erosion to 20% of normal. 'Desert Mult' - default 1.5 means deserts erode 50% faster. There are also multipliers for sand and coastal areas.

> For **biome spreading**, the rules are based on neighbor counting. Grass becomes forest if it has enough forest neighbors (controlled by 'Forest Threshold' in UI) and passes a probability check ('Forest Chance'). Same pattern for desert, wetland, and the mountain biomes.

> Height also matters: below 0.30 is forced to water, above 0.85 is always snow. The 'Tree Line Height' parameter controls where forests transition to tundra."

---

## Slide 6: Biome System (30 seconds)

> "Here are the 9 biome types in the system.

> Water is forced below a certain height. Sand appears in coastal zones. Grass is the default land cover and can convert to forest.

> Forest is the key biome - it spreads to neighbors and resists erosion. Desert spreads in dry areas and *accelerates* erosion.

> At high elevations we have rock, snow on peaks, tundra as a transition zone, and wetlands in low areas near water."

---

## Slide 7: Performance Results (45 seconds)

> "Let's talk performance.

> I benchmarked on an RTX 2080 across four grid sizes. At 512×512 - that's 262,000 cells - we hit over 3,000 FPS. At 1024×1024, about 1,400 FPS.

> Even at 3072×3072 - that's **9.4 million vertices** being simulated and rendered - we maintain 243 FPS. That's well above the 60 FPS target for interactive applications.

> The key insight from this chart is that **FPS scales linearly with grid area**. Doubling the grid dimensions means roughly quartering the FPS, which is expected behavior for a GPU-bound workload."

---

## Slide 8: Scalability Analysis (30 seconds)

> "This chart shows what happens when we increase simulation speed.

> At higher simulation speeds, we run more compute iterations per rendered frame. At 1000× speed, we see about a 30% FPS reduction.

> But even at extreme simulation speeds, performance remains interactive. The system is well-optimized for the GPU's parallel architecture."

---

## Slide 9: Demo Video (60 seconds)

> "Let me show you a quick demo of the system in action."

*[Play walkthrough_demo.mp4]*

> "You can see terrain generation from noise, camera controls for navigation, spawning biomes with mouse clicks, and crucially - real-time parameter adjustment. I can change erosion rates, biome spreading probabilities, all while the simulation keeps running."

---

## Slide 10: Future Work (30 seconds)

> "For future work, the main addition would be **hydraulic erosion** - simulating actual water flow and sediment transport, not just mass movement.

> **Infinite terrain** using chunked streaming would allow much larger worlds.

> And **advanced lighting** with shadows and ambient occlusion would significantly improve visual quality."

---

## Slide 11: Thank You (15 seconds)

> "That's Living Worlds. The code is available on GitHub at this repository.

> Thank you for listening. I'd be happy to take any questions."

---

## Total Time: ~7 minutes + 1 minute demo = ~8 minutes
