# ADR-0001: Dynamic Engine System & Lifecycle Parity

## Status
Accepted

## Context
ArcadeMatrix runs on embedded targets (ESP32 standard and Waveshare ESP32-S3) driving RGB LED panels. Prior architecture relied on hardcoded engine instantiation, direct manipulation from `main.cpp`, and tight coupling between display loops and specific engines.

On the reference Raspberry Pi implementation, an auto-discovery and lifecycle contract (`IEngine` / `EngineDescriptor` / `EngineRegistry`) allows dynamic engine discovery, schema declaration, lazy instantiation, and live configuration updates (`onConfigChanged`).

## Decision
1. Adopt a strict `IEngine` interface on ESP32:
   - `initialize(EngineContext* context, const EngineConfig* config)`
   - `activate()`
   - `update(EngineContext* context)`
   - `render(EngineContext* context)`
   - `deactivate()`
   - `onConfigChanged(const EngineConfig* config)`
   - `isFinished() -> bool`
   - `isRealtime() -> bool`
   - `selfPaced() -> bool`
   - `setRotationBudget(uint32_t budget)`
   - `allowsOverlay() -> bool`

2. Centralize registration via `EngineRegistry` and `EngineRegistrar::registerAll()`.
3. Eliminate direct `new XxxEngine()` allocations in `main.cpp` or runtime loops. All instances are constructed dynamically via descriptor factories.
4. Manage life cycles using `std::unique_ptr<IEngine>` with lazy instantiation upon first activation.

## Consequences
- Clean separation of concerns between framework, hardware, and individual engines.
- New engines can be added in a single location by declaring metadata, schema, capabilities, and requirements.
- Memory footprint is minimized since only active rotation engines are instantiated on demand.
