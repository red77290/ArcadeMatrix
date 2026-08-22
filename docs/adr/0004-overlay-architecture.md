# ADR-0004: Display Arbiter & Additive Overlay Architecture

## Status
Accepted

## Context
In early firmware iterations, the M.U.G.E.N `FighterEngine` was treated as a regular rotation engine slot or tied to hardcoded background checks (`if (mod=="clock"||"date"||"weather")`). This caused two major issues:
1. When active, it replaced the entire screen instead of acting as an animated combat sprite overlay over clock/weather faces.
2. The core display loop contained engine-specific string matching.

## Decision
1. Clarify the role of `DisplayArbiter` priorities:
   - Priority sources: `MQTT` > `Marquee` > `Message` > `GIF` > `Visualizer` > `Rotation`.
2. Treat `FighterEngine` (and future sprite/notification overlays) as an **additive compositing pass**:
   - The primary active engine executes `render()`.
   - If the active engine declares `allowsOverlay() == true` and `fighter_main.enabled == true`, the overlay executes `composite()` / `render()` on top of the existing matrix buffer without calling `matrix.fillScreen(0)`.
   - When a non-rotation source takes priority (e.g. MQTT or Marquee) or when an engine with `allowsOverlay() == false` is active (e.g. GIF Player), overlays are automatically disabled and unloaded from memory.
3. Remove all engine name checks (`if (mode == "gifs")`, `if (mod == "clock")`) from `RotationManager` and display loops.

## Consequences
- Clean, extensible layering architecture suitable for future overlays (e.g. weather animations, notifications, sprite mascots).
- Zero frame-buffer clearing during overlay rendering, preventing visual flicker.
- Clear arbitration hierarchy between primary display requests and secondary overlays.
