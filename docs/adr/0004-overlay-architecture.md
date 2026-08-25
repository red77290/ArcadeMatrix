# ADR-0004: Display Arbiter & Transverse Overlay Architecture

## Status
Accepted

## Context
In early firmware iterations, the M.U.G.E.N `FighterEngine` was treated as a selectable rotation engine slot or tied to hardcoded engine exceptions (`allowsOverlay`). This caused conceptual and runtime issues:
1. `Fighter` is not a standalone selectable display source, but a transverse decorative overlay.
2. Hardcoding capabilities like `allowsOverlay` artificially prevented valid user combinations such as `GIF + Fighter`.
3. Frequent instance allocations/deallocations on rotation switches could cause heap fragmentation on ESP32.

## Decision
1. **Separation of Concerns:**
   - `DisplayArbiter`: Selects the primary display source (e.g. `MQTT` > `VISUALIZER` > `MARQUEE` > `GIF` > `ROTATION`).
   - `EngineRegistry`: Contains only primary selectable display sources (`clock`, `date`, `weather`, `gifs`, `crypto`, `stock`, `audiovisualizer`, `decibelMeter`, `temp`, `message`).
   - `OverlayManager`: Manages transverse overlays (`FighterEngine`).
2. **User-Driven Per-Rotation Configuration:**
   - Overlays are enabled per rotation slot via `"overlays": { "fighter": true }` (migrated from legacy `"fighter_overlay"`).
   - Combined with global master switch: `isActive = global_enabled && rotation_entry.overlays.fighter`.
   - Zero engine-specific exceptions: `GIF + Fighter` is completely supported.
3. **Additive Invariant:**
   - Overlays compose additively over existing framebuffers and must never call `matrix.clear()`.
4. **Heap Preservation on ESP32:**
   - `OverlayManager` lazily instantiates `FighterEngine` on first demand and preserves the instance in heap across rotation cycles.

## Consequences
- Clean, extensible layering architecture where overlays are independent of primary engine types.
- Zero visual flicker through additive compositing.
- Zero heap fragmentation on ESP32.
