# Changelog

All notable changes to the **ArcadeMatrix (ESP32)** firmware, WebUI, and tooling are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [4.0.3] - 2026-10-14

### Added
- **Modular Configuration Domain Architecture**: Split persistent SD configuration into `/config/{hardware,system,network,playlist}.json` and per-instance `/config/instances/<id>.json` with atomic temporary file replacement and brownout recovery.
- **MqttDataEngine (`mqttdata`)**: Zero-allocation, wait-free seqlock + reader-refcounted MQTT data engine supporting values, tables, weather, and sparkline/bar graphs for Home Assistant integration.
- **Retro Clock Faces & Responsive Tate Layouts**: Added NES Koopa Troopa dual-mode walking/shell sprites, Pokedex, Castle, Megaman, Mario, Pac-Man, Versus, Slot Machine, Words, and Tetris clock layouts across `128x32`, `64x32`, `64x64`, and 32-wide Tate orientations.

### Changed
- **Two-Stage Engine Lifecycle & Anti-UAF Release Barrier**: Separated non-blocking Core 1 logical deactivation (`deactivate()`) from Core 0 asynchronous physical teardown (`shutdownForDestruction()`) via `EngineRetirementQueue` and `Core0LifecycleDispatcher`.
- **Canvas-Isolated HUB75 Presentation Pipeline**: Unified all rendering through `IDrawingSurface`, `CanvasBufferedSurface`, `DirectDmaSurface`, and template-specialized `Hub75BulkEncoder` with SPIRAM cache writeback on ESP32-S3.
- **Reference Capability & Compatibility Evaluator**: Introduced deterministic admission gating (`CompatibilityEvaluator`) against hardware reference profiles (`ESP32_STD` vs `WAVESHARE_S3`) and target pipelines.

### Fixed
- **Core 1 Hot-Path & Lifecycle Hardening**: Fixed premature `Core0LifecycleDispatcher::hasPending()` return during engine retirement, eliminated per-frame `String` dangling pointers (`ArcadeClock`, `PacmanClock`) and Core 1 `actionMutex` contention, and resolved cross-task TLS socket abort races.
- **REST API & WebUI Security/Reliability**: Fixed raw marquee buffer memory leak, blocked path traversal on `/marquees/`, synchronized all live config mutations through `config.mutate()`, and routed all WebUI network calls through `HttpRequestQueue`.

## [4.0.0] - 2026-09-15

### Added
- **v4 Presentation & Memory Architecture**: Added `IDrawingSurface`, `Hub75PresentationBackend`, `NetworkBudget` TLS admission controller, and `MemoryManager` domain health telemetry.
- **3-Level Icon Cache (`IconService`)**: Replaced heavy PNG decoding on classic ESP32 with lightweight `JPEGDEC` + SD `.rgb565` binary caching and RAM LRU cache.

## [3.4.0] - 2026-08-20

### Added
- **Autonomous Audio Subsystem (`AudioHub`)**: Added self-hibernating `WebRadioService`, `DLNAService`, `AirPlayAudioService`, `BluetoothAudioService`, and `MusicEngine`.
- **Gyroscope Auto-Rotation (`GyroHAL` & `DisplayOrientationManager`)**: Added live orientation detection (`QMI8658`, `MPU6050`, `LSM6DS3`, `LIS3DHTR`) and hardware transition FX.

## [3.2.4] - 2026-08-01

### Added
- **Dynamic Engine Registry & Schema-Driven WebUI**: Added `EngineRegistry`, `IEngineDescriptorHandler`, and compile-time catalog injection into `WebUI.h`.
- **SRSW Lock-Free Triple-Buffered Configuration**: Added `ConfigSnapshot` and `ConfigSnapshotGuard` for zero-mutex configuration reads on Core 1.

