🇬🇧 English | 🇫🇷 [Français](DEVELOPER_FR.md) | 🇪🇸 [Español](DEVELOPER_ES.md)

# Developer Guide (ESP32 — C++)

This is the **complete, exhaustive** guide to extending ArcadeMatrix on ESP32 (written in **C++**). It explains the `IEngine` contract in full, the entire `ConfigField` schema (including **dynamic/custom option lists**, multiselect, conditional visibility and self-healing validation policies), hardware capability gating, and walks through building a new engine end-to-end.

> For the *why* behind the architecture (Registry, Lazy-Once, DisplayArbiter, FreeRTOS threading, overlay compositing), read [ARCHITECTURE.md](ARCHITECTURE.md). This guide is the practical *how-to*.

---

## Table of Contents

1. [Mental Model](#1-mental-model)
2. [The IEngine Contract in Full](#2-the-iengine-contract-in-full)
3. [The Lifecycle & Golden Rules](#3-the-lifecycle--golden-rules)
4. [Capabilities & Hardware Requirements](#4-capabilities--hardware-requirements)
5. [The ConfigSchema & ConfigField Reference](#5-the-configschema--configfield-reference)
6. [Custom / Dynamic Option Lists (`options_endpoint`)](#6-custom--dynamic-option-lists-options_endpoint)
7. [Multiselect Fields](#7-multiselect-fields)
8. [Conditional Fields (`visible_when`)](#8-conditional-fields-visible_when)
9. [Self-Healing Validation Policies](#9-self-healing-validation-policies)
10. [Tutorial: Create a New Engine Step-by-Step](#10-tutorial-create-a-new-engine-step-by-step)
11. [Tutorial: Add a Custom-List Endpoint](#11-tutorial-add-a-custom-list-endpoint)
12. [Tutorial: Add a New Clock Face / Theme Step-by-Step](#12-tutorial-add-a-new-clock-face--theme-step-by-step)
13. [Internationalization & Centralized i18n (Front & Back)](#13-internationalization--centralized-i18n-front--back)
14. [Reading Config in an Engine](#14-reading-config-in-an-engine)
15. [Rendering into the LED Matrix](#15-rendering-into-the-led-matrix)
16. [Testing & Local Compilation](#16-testing--local-compilation)
17. [Developer Checklist](#17-developer-checklist)

---

## 1. Mental Model

ArcadeMatrix has **no hardcoded list of display features** in `main.cpp`. Each engine is a decoupled plugin registered at startup in the central `EngineRegistry`.

```mermaid
flowchart TD
    subgraph EngineModule["Your Engine Module (src/engines/MyEngine.*)"]
        ENG["class MyEngine : public IEngine"]
        HND["class MyEngineDescriptorHandler : public IEngineDescriptorHandler"]
        HND -.->|"factory builds"| ENG
    end

    subgraph Registration["Engine Registrar (src/engines/EngineRegistrar.cpp)"]
        REGT["EngineRegistrar::registerAll()"]
        REGT --> CALL["EngineRegistrar::registerHandler(handler)"]
        CALL --> GET["handler.getDescriptor()"]
        CALL --> GATING{HardwareHAL Meets Requirements?}
    end

    subgraph Core["Engine Registry & Consumer"]
        GATING -->|"Yes"| REG["EngineRegistry (Active Factory)"]
        GATING -->|"No"| REG2["EngineRegistry (available=false + reason)"]
        REG --> API["GET /api/engines (Web UI Auto-Form)"]
        REG --> RM["RotationManager (Lazy-Once Instance)"]
        RM --> SCREEN["HUB75 LED Matrix (DMA Buffer)"]
    end

    HND --> CALL
```

Adding an engine requires **two simple steps**:
1. Implement your engine class (`IEngine`) and its companion descriptor handler (`IEngineDescriptorHandler`) in `src/engines/`.
2. Add your descriptor handler instance to the handlers array in `src/engines/EngineRegistrar.cpp`.

> [!NOTE]
> **Why `IEngineDescriptorHandler` on ESP32?**
> Rather than a monolithic registrar with hardcoded schemas (God Class), each engine defines and encapsulates its own metadata, config schema, requirements, and factory. The `EngineRegistrar` then automatically iterates over all handlers and performs runtime hardware gating before registering into `EngineRegistry`.

**`main.cpp` and WebUI HTML files are never edited.**

---

## 2. The IEngine Contract in Full

```cpp
class IDisplayGeometryAware {
public:
    virtual ~IDisplayGeometryAware() = default;
    virtual void onDisplayGeometryChanged(const DisplayGeometry& geometry) = 0;
};

class IEngine : public IDisplayGeometryAware {
public:
    virtual ~IEngine() = default;

    // --- Mandatory lifecycle ---
    virtual EngineError initialize(EngineContext* context, const EngineConfig* config) = 0;
    virtual void activate() = 0;
    virtual void update(EngineContext* context) = 0;
    virtual void render(EngineContext* context) = 0;
    virtual void deactivate() = 0;

    // --- Core 0 physical destruction lifecycle ---
    virtual bool shutdownForDestruction() { return true; }

    // --- Preemption lifecycle (optional hooks for temporary interruptions) ---
    virtual void pause() {}
    virtual void resume() {}

    // --- Optional (safe defaults provided) ---
    virtual void onConfigChanged(const EngineConfig* config) {}
    virtual void onDisplayGeometryChanged(const DisplayGeometry& geometry) override {}
    virtual bool isFinished() const { return false; }
    virtual bool isRealtime() const { return true; }
    virtual void setRotationBudget(uint32_t budget) {}
    virtual bool selfPaced() const { return false; }
    virtual bool allowsOverlay() const { return true; }
};
```

| Method | Default | When to Override |
| :-- | :-- | :-- |
| `initialize()` | — | **Always.** Pre-allocate buffers, decode static bitmaps, load fonts. |
| `activate()` | — | **Always.** Cheap reset of transient state (chronometers, frame index). |
| `update()` | — | **Always.** Business and animation logic each frame. |
| `render()` | — | **Always.** Draw pixels into `context->getSurface()`. |
| `deactivate()` | — | **Always.** Core 1 non-blocking logical quiescence: detach surface, abort sockets, set state flags. Zero waits, zero allocations. |
| `shutdownForDestruction()` | `return true;` | **If engine has background tasks.** Core 0 physical quiescence: wait cooperatively for worker tasks to exit, release task stack and DMA buffers before instance deletion. |
| `pause()` | no-op | **Optional.** Called when temporarily preempted by a high-priority alert or message. Preserves internal state. |
| `resume()` | no-op | **Optional.** Called when returning from a temporary preemption without losing animation phase or timers. |
| `onConfigChanged()` | no-op | **If engine has settings.** Re-read values in place without recreation. |
| `onDisplayGeometryChanged()` | no-op | **If engine maintains geometry-derived caches** (e.g. column arrays). |
| `isFinished()` | `false` | If engine has an intrinsic end (e.g. cycle completed) to advance rotation early. |
| `isRealtime()` | `true` | Return `true` for 60 FPS animations; return `false` for static 20 FPS displays. |
| `setRotationBudget()`| no-op | If count-based (e.g. play N GIFs). Receives the rotation entry count. |
| `selfPaced()` | `false` | If true, duration timer does not force-advance; engine drives advance via `isFinished()`. |
| `allowsOverlay()` | `true` | Return `false` for bandwidth-heavy engines (e.g. `GifEngine`) to bypass overlay compositing overhead and maintain maximum framerate. |

---

## 3. The Lifecycle & Golden Rules

```mermaid
stateDiagram-v2
    [*] --> Initialized : factory() + initialize() (Once on first display)
    Initialized --> Active : activate() (Rotation transition)
    Active --> Active : update() + render() (60 FPS Hot Loop)
    Active --> Active : onConfigChanged() (Live WebUI edit)
    Active --> Paused : pause() (Temporary high-priority preemption)
    Paused --> Active : resume() (Returning from preemption)
    Active --> Standby : deactivate() (Rotation slot end)
    Standby --> Active : activate()
    Active --> [*] : isFinished() / timeout advances rotation
```

### Display Decision, Lifecycle & Preemption Matrix

The `DisplayArbiter` resolves display sources deterministically via a static priority scale and dispatches decisions to `DisplayRuntime`:

| Scenario | Action on Outgoing Engine | Action on Incoming Engine | Session State |
| :--- | :--- | :--- | :--- |
| **Temporary Preemption** (e.g. MQTT Alert on Clock) | `oldEngine->pause()` | `alertEngine->activate()` | Session ID increments; previous engine pinned for resume |
| **End of Preemption** (Returning to Clock) | `alertEngine->deactivate()` | `oldEngine->resume()` | Session ID increments; baseline engine resumed in-place |
| **Carousel Rotation** (e.g. Clock → Weather) | `oldEngine->deactivate()` | `newEngine->activate()` | Session ID increments; previous session cleanly torn down |

### Golden Rules for Embedded C++

1. **Golden Rule #1 — Zero Heap Allocation in Hot Loop:**
   Never instantiate `String`, `std::vector`, or call `malloc`/`new` inside `update()` or `render()`. Pre-allocate all buffers in `initialize()` and mutate in place.
2. **Golden Rule #2 — Lock-Free Hot Path & Zero Mutex on Core 1:**
   Core 1 runs `update() -> evaluate() -> render()` completely lock-free. Configuration is accessed exclusively via the linearizable Single-Reader Single-Writer (SRSW) CAS protocol (`ConfigSnapshotGuard guard = config.acquireSnapshot(); const auto& snapshot = guard.get();`).
3. **Golden Rule #3 — Single Producer SPSC Cross-Core Commands:**
   Core 0 submits display requests via `m_displayArbiter.submitRequest(req)`. Core 1 owns the arbiter slots exclusively and drains commands in $O(1)$ without mutex contention.
4. **Golden Rule #4 — In-Place Hot Reload:**
   In `onConfigChanged()`, update internal variables directly. The instance is **not** destroyed or recreated.
5. **Golden Rule #5 — Coarse-Grained SD Ownership, Never Per-Read Locking:**
   `sdMutex` is a **non-recursive** FreeRTOS mutex (`xSemaphoreCreateMutex()`). Take it **once**, around a complete SD transaction (open, directory scan, whole-file read, close), and never inside a callback that the same transaction can re-enter: `AnimatedGIF` invokes its read/seek callbacks synchronously from `gif.open()`, so locking there self-deadlocks.
   Once a streaming handle is open, it belongs exclusively to Core 1 for the lifetime of the playback session and is read **without** locking, as required by Golden Rule #2.
   Core 0 producers (HTTP handlers, MQTT) must always use a **bounded** wait (`pdMS_TO_TICKS(...)`) and degrade gracefully. `portMAX_DELAY` on the AsyncTCP task freezes the entire web server.
6. **Golden Rule #6 — Overlays vs Selectable Engines:**
   - **Selectable Engine:** Replaces the primary framebuffer (e.g. Clock, Weather, GIF, Crypto). Registered in `EngineRegistry` with a descriptor, factory, and canonical `EngineHandle`.
   - **Transverse Overlay:** Composites additively on top of any active display source (e.g. Fighter). Managed exclusively by `OverlayManager`, enabled per rotation slot (`overlays.fighter: true`), never registered in `EngineRegistry`.
7. **Golden Rule #7 — Stateful Network Protocols & Socket Lifecycle (CastV2 / TLS):**
   - Transversal network streaming engines (e.g. `GoogleCastEngine`) MUST maintain a persistent `WiFiClientSecure` connection across polling cycles with active protocol heartbeats (CastV2 `PING` every 5s on `urn:x-cast:com.google.cast.tp.heartbeat` to `receiver-0`).
   - NEVER instantiate and teardown TLS clients in a rapid polling loop (e.g. 1-2s): TCP `TIME_WAIT` states linger for 120 seconds in lwIP. Sockets accumulate up to the OS ceiling (`fd 48`, `ECONNABORTED = 113`), starving `AsyncWebServer` (port 80) and mDNS, resulting in `ERR_ADDRESS_UNREACHABLE`.
   - Reconnect backoff after connection failure MUST be at least 15 seconds to bound maximum concurrent `TIME_WAIT` descriptors to 8 (well below the 48 socket ceiling).
   - **Known limitation:** even with serialization (Golden Rule #11), Google Cast can be denied TLS admission indefinitely once internal DRAM settles into a fragmented steady state (largest block below ~16 KB) with other engines active — mDNS discovery succeeding while the handshake never completes currently presents as "Cast not working". This is accepted graceful degradation, not yet a real fix.
8. **Golden Rule #8 — Resource Hierarchy: Critical Services vs Opportunistic Overlays:**
   - Critical services (Display matrix, Core 0 Web Server, Audio stream, Transversal Cast, SDMMC) have reserved bandwidth and memory.
   - Opportunistic and decorative overlays (`FighterEngine`, `ArtworkService`) MUST be strictly subordinate:
     * Never compete with critical services for RAM, DMA, or bus bandwidth.
     * Cut short immediately upon the first file failure or memory dip (`heap < 30 KB`, `dma < 16 KB`, `psram < 1 MB`), free any partial allocations, and abort without attempting remaining files.
     * Use `NetworkBudget::canStartTlsSession()` before initiating any optional HTTPS/TLS download.
9. **Golden Rule #9 — Hardware DMA Gating for Crypto & Storage:**
   - Hardware SHA (`esp-sha`) in ESP32-S3 and SDMMC block reading require contiguous internal DMA memory (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`). If internal DMA drops below 16 KB or largest DMA block drops below 4096 bytes, `esp-sha: Failed to allocate buf memory` and `sdmmc_read_blocks failed (257) (ESP_ERR_NO_MEM)` will occur.
   - `NetworkBudget::canStartTlsSession()` must evaluate internal DMA headroom (`freeDma >= 16 KB`, `largestDma >= 4 KB`) as well as total DRAM before admitting TLS handshakes.
10. **Golden Rule #10 — Single-Path Peripheral Recovery (Core 0 Exclusive):**
    - Never run parallel recovery watchdogs across cores.
    - If Core 1 detects hardware peripheral anomalies (e.g. ES7210 digital zero freeze), Core 1 evaluates lock-free in $O(1)$ and signals an atomic flag.
    - Recovery (I2C re-initialization) is executed exclusively on Core 0 with bounded rate-limiting/cooldown (3000 ms), completely isolated from the Core 1 audio rendering hot-path.
11. **Golden Rule #11 — mbedTLS Stays 100% Internal DRAM; Admit & Serialize, Never Route TLS to PSRAM:**
    - mbedTLS on this board MUST use the stock ESP-IDF/Arduino allocator (100% internal DRAM), matching `v3.1.0` behavior. **Do not** reintroduce a PSRAM-routing `mbedtls_platform_set_calloc_free()` hook (the previous `MbedTlsAllocator` has been deleted for this exact reason): live hardware testing proved that ANY mbedTLS allocation landing in PSRAM — even only the ~16 KB TLS record buffers — corrupts the HUB75 display within seconds, because the framebuffer is also PSRAM-resident and contends with mbedTLS over the shared PSRAM cache via the HUB75 GDMA engine. See `HardwareHAL::begin()` for the full root-cause comment.
    - Since internal DRAM must now serve TLS, DMA, and networking simultaneously, every TLS call site MUST construct a `NetworkBudget::ScopedTlsHandshakeLock` immediately before `WiFiClientSecure::connect()` and bail out (`if (!tlsLock) { ...; return; }`) if it fails to acquire it. This is a single global mutex: **only one TLS handshake may be in flight anywhere in the firmware at a time.**
    - Hard admission boundary, re-validated **atomically inside the lock's constructor** (not just as a separate pre-check): `NetworkBudget::canStartTlsSession()` requires `freeInternal >= 45 KB` and verified dual-buffer headroom (either `largestInternalBlock >= 35,000 B` or a successful live probe of two independent 16,717 B blocks) to satisfy mbedTLS's concurrent in/out record buffers (2 x 16,717 B = 33,434 B total). A pre-check before even attempting the lock is allowed as a cheap optimization to avoid blocking on a contended mutex when the budget is already known-insufficient, but it is **never** authoritative by itself — mutex-contention wait time (up to 5s) is enough for a concurrent handshake to invalidate an earlier "OK" result. Only the post-acquire re-check inside the constructor is authoritative.
    - *Security Note:* since mbedTLS is internal-DRAM-only again, there is no PSRAM-residency concern for TLS-sensitive material to document.
12. **Golden Rule #12 — SD Access via `SdLockGuard`, Never Manual `xSemaphoreTake`/`Give`:**
    - Use `SdLockGuard guard(timeoutTicks); if (!guard) { ...; return; }` (`src/core/SdLockGuard.h`) for every `sdMutex` acquisition. It is a move-free RAII wrapper that guarantees the mutex is released on every return path (including early returns), eliminating the lock-leak risk of hand-written `xSemaphoreTake(...) ... xSemaphoreGive(...)` pairs that must otherwise be duplicated on every exit path.
    - This does not relax Golden Rule #5: still take the lock coarse-grained around a whole SD transaction, never inside per-frame/per-byte streaming callbacks (`GIFReadFile`/`GIFSeekFile` deliberately remain **unlocked**, since the file handle is opened once under `SdLockGuard` and then owned exclusively by Core 1 for the rest of the playback session).
13. **Golden Rule #13 — Engine Retirement Release Barrier (Anti-UAF):**
    - Before an engine's `unique_ptr` is moved into the `EngineRetirementQueue` for Core 0 destruction, `RotationManager::retireEngineSlot()` MUST, in order: (1) call `deactivate()`, (2) clear `currentActiveInstanceId` if it matches, (3) call `DisplayRuntime::purgeEngineReferences(engine, instanceId)` to strip the pointer from `m_session.activeEngine` and the preemption stack, (4) mark `EngineResourceState::CORE1_RELEASED`, (5) sever the local `instanceId` slot. Only after all five steps may the engine be handed off to Core 0. Never duplicate this sequence inline elsewhere — always call `retireEngineSlot()`.
14. **Golden Rule #14 — Memory Domain Segregation & PSRAM-First for Large Transient Buffers:**
    - Move application-owned JSON document allocations (`WebServerAPI`, REST endpoints) to PSRAM via `SpiRamJsonDocument`; AsyncTCP/lwIP and server-internal allocations remain in their required internal DRAM domains.
    - Large non-DMA graphical framebuffers (such as `GifEngine`'s 32 KB canvas) MUST prioritize PSRAM allocation (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`) when PSRAM is available, reserving internal DRAM for mbedTLS and LwIP networking.
    - AsyncTCP background worker stack is sized to 8192 bytes and strictly pinned to Core 0 (`CONFIG_ASYNC_TCP_RUNNING_CORE=0`) to protect the Core 1 display rendering hot-path (Invariant 1).
    - Persistent network services (Google Cast) MUST implement exponential backoff (5s, 10s, 20s, 60s) initialized upon session drop, preventing reconnection storms during bursty HTTP/LwIP activity.
15. **Golden Rule #15 — Pure Drawing via `IDrawingSurface` & DMA Isolation (Invariants 18 & 19):**
    - An engine is a pure algorithm: it MUST NEVER call `context->getMatrix()->fillScreen(0)` or manipulate the physical DMA framebuffer directly.
    - All drawing operations MUST target `context->getSurface()`. Mutating primitives (`drawPixel`, `blit565`, `fillRect`, `clear`) automatically mark the surface as dirty via `markModified()`.
    - If an engine has no new frame or is static between updates, `present()` is a zero-cost no-op without DMA transfer, completely eliminating single-buffer DMA screen flickering.
16. **Golden Rule #16 — Allocation-Free & Quiescent Deactivation (Invariants 15 & 16):**
    - `deactivate()` MUST NOT perform any new dynamic memory allocation (`malloc`, `new`, container resize). Reclaim memory using `std::vector<T>().swap(vec)` or `{}` rather than non-binding `shrink_to_fit()`.
    - `deactivate()` executes on Core 1 strictly non-blocking to guarantee **logical rendering quiescence** (immediately halting all draw commands and detaching the surface). Complete background task, timer, and network socket termination is performed on Core 0 via `shutdownForDestruction()` prior to releasing shared resources. The system returns to the reference idle baseline within the Quiescent Baseline Envelope ($|\Delta \text{heap}| \le 2\text{ KB}$).
17. **Golden Rule #17 — Network Quiescence & Scoped Socket Abort (Invariant N8):**
    - Network engines MUST support immediate cooperative cancellation (`session.abort()` / `_client.stop()`).
    - `deactivate()` MUST stop Core-1 rendering interaction and signal session cancellation without waiting or blocking on network sockets.
    - `shutdownForDestruction()` MUST abort/join engine-owned background workers and finalize network resource quiescence on Core 0 before shared resources are released.
    - Once a session is aborted and its owner is physically quiescent, no further application processing, callbacks, JSON parsing, or allocation may occur on that session.
18. **Golden Rule #18 — Dynamic Presentation Pipeline & Color Depth Adaptation (Invariant 21):**
    - Engines must not assume permanent static color depth. When switching between rich graphical engines (up to 8 bits configured) and memory-intensive TLS engines (4 bits nominal), the presentation pipeline deterministically reconfigures under hardware OE blanking ($< 30\text{ ms}$).
    - **P0 Guarantee:** Output Enable (OE) is released (LOW) strictly after Frame 0 is rendered and committed (`firstFrameCommitted == true`). If target and progressive fallback allocations fail, OE remains HIGH (`PresentationRecovery`).
    - Telemetry distinguishes `requestedDepth` (policy), `effectiveDepth` (actual), and `fallbackUsed = (effectiveDepth != requestedDepth)`.
    - `FastMatrixPanel::initLuts(depth)` dynamically recalculates per-channel gamma lookup tables to prevent color corruption at reduced bit depths (see [MEMORY_MODEL.md](MEMORY_MODEL.md) and [MEMORY_OPTIMIZATIONS.md](MEMORY_OPTIMIZATIONS.md)).
19. **Golden Rule #19 — Network Transaction Consolidation & Keep-Alive Batching:**
    - Network engines querying multiple data points (e.g. market quotes, weather forecasts) MUST NOT make individual, sequential TLS connections.
    - If multiple items are available on a single REST endpoint, use multi-symbol batch parameters (e.g. CoinGecko comma-separated GET).
    - If multiple requests to the same host are required, reuse a persistent TLS session with HTTP/1.1 keep-alive (`net::SecureHttpSession session("host"); session.get(...)`), performing **one TLS handshake per successful keep-alive batch session**.
    - Implement cache-miss consolidation: when one item is fetched, refresh all configured items in that single session so subsequent rotations hit RAM cache with zero network latency.
20. **Golden Rule #20 — 3-Level Icon Cache & Proscription of PNGdec on Classic ESP32:**
    - `PNGdec` embeds a 32 KB internal zlib window (`sizeof(PNG) = 34,288 B`). Calling `new PNG()` on classic ESP32 without PSRAM while a 4-bit panel is active is guaranteed to trigger `std::bad_alloc`. Dynamic `new PNG()` is **strictly prohibited** on classic ESP32.
    - All market and UI icons MUST use `IconService`:
      * **L1 (RAM Cache):** In-memory RGB565 bitmaps for instant rendering.
      * **L2 (SD Cache):** Persistent local cache (`/crypto_icons/`, `/stock_icons/`).
      * **L3 (Network Proxy):** Plain HTTP fetch via `images.weserv.nl` transcoded to JPEG, decoded via `JPEGDEC` in ~2.5 KB RAM.
      * If proxy or network is unavailable, fall back to SD cache or render text gracefully without an icon.
21. **Golden Rule #21 — Presentation-Aware Deferred Network Polling:**
    - On non-PSRAM hardware (`!psramFound()`), background network polling MUST be deferred while a 4-bit panel is actively presenting if initial data is already cached. This eliminates transient heap contention and prevents visual micro-stuttering.
22. **Golden Rule #22 — Boot Sequencing & Persistent System Zone Packing:**
    - All permanent system allocations (Wi-Fi driver, STA association, DHCP negotiation, public DNS, mDNS with its 4 KB task stack, SNTP client, WebServerAPI route closures with its 8 KB `async_tcp` worker task, AudioHub, Core0LifecycleDispatcher "Lifecycle0" with 3 KB task stack) MUST be initialized in Step 3 *before* display matrix allocation (`matrixEngine.begin()`).
    - This packs all permanent memory in lower DRAM (`0x3ffe0000..0x3ffee000`), ensuring that the Volatile Sandbox Zone (`0x3ffee000..0x3fffffff`) remains completely contiguous and can coalesce back to 50–65 KB upon panel release.
23. **Golden Rule #23 — Transition-Window Prefetching & Mid-Presentation TLS Gating:**
    - Engines requiring remote data and historical points (e.g. `StockEngine`, `CryptoEngine`) MUST implement `prefetchData()` to retrieve quotes and charts in the clean transition window with DMA released (where 70 to 90 KB is free), before the target display panel is allocated.
    - Presentation loops (`update()`) MUST render strictly from cache without executing blocking TLS handshakes. Mid-presentation TLS is gated by `NetworkBudget::canStartTlsSession()` enforcing `largest >= TLS_MIN_COMBINED_BLOCK` (40 KB).
    - `deactivate()` must leave **zero survivor strings or buffers** (e.g. `GifEngine` clearing `lastPlayedGif` and swapping vectors) to preserve the contiguous sandbox block.

---

## 4. Capabilities, Granular Memory Footprint & Allocation Prediction

Declared in the engine descriptor, static capabilities and requirements inform the runtime, WebUI, and `CompatibilityEvaluator` about hardware dependencies, presentation budgets, and precise memory footprints:

```cpp
struct EngineCapabilities {
    bool supports_128x32 = true;
    bool supports_256x64 = true;
    bool realtime = true;
    bool interruptible = true;
    bool selfPaced = false;
    bool allowsOverlay = true;          // Permits transverse overlays (e.g. Fighter)
    bool allowRotation = true;          // Appears in WebUI rotation sequence
};

struct EngineRequirements {
    // --- Hardware Peripheral Dependencies (Hard Constraints) ---
    bool needsPsram = false;            // External SPIRAM strictly required
    bool needsPsramDma = false;         // DMA-capable SPIRAM required (ESP32-S3)
    bool needsAudio = false;            // Audio hardware required (alias)
    bool needsAudioInput = false;       // I2S Microphone required (Decibel, Spectrum)
    bool needsAudioOutput = false;      // I2S DAC/Speaker required
    bool needsI2s = false;              // General I2S bus required
    bool needsTempSensor = false;       // SHTC3 temperature sensor required
    bool needsGyroscope = false;        // QMI8658 IMU required
    bool needsNetwork = false;          // Active Wi-Fi network connection required
    bool needsTls = false;              // TLS/HTTPS cryptographic handshake required
    bool needsSd = false;               // MicroSD card storage required

    // --- Presentation & Pipeline Constraints ---
    bool requiresDoubleBuffer = false;  // Cannot tolerate transient blanking or tearing
    bool prefersDoubleBuffer = false;   // Prefers tear-free double buffer, runs degraded in single buffer
    bool supportsSingleBuffer = true;   // Allows running under single buffer
    uint16_t targetFps = 60;            // Target presentation framerate (60, 30, 10, 1)

    // --- Granular Memory Footprint Modeling ---
    uint32_t internalPersistentBytes = 0;   // Static heap retained across frames
    uint32_t internalContiguousBytes = 0;   // Largest single contiguous allocation needed
    uint32_t psramBytes = 0;                // Dedicated working buffer in SPIRAM
    uint32_t shadowBytesPerFrame = 0;       // Dynamic canvas/shadow memory (must be 0 on hot loop)
    uint32_t minFreeInternalHeapBytes = 0;  // Conservative heap headroom floor
    uint32_t minLargestInternalBlockBytes = 0;
    uint32_t minFreeDmaBytes = 0;
    uint32_t minFreePsramBytes = 0;

    // --- Geometry Limits ---
    uint16_t minWidth = 0;
    uint16_t minHeight = 0;
    uint16_t maxWidth = 0;              // 0 = unlimited
    uint16_t maxHeight = 0;             // 0 = unlimited
};
```

---

### 4.1 Granular Memory Footprint Modeling (`EngineRequirements`)

ArcadeMatrix V4 replaces heuristic memory checks with **deterministic memory modeling**. Every engine descriptor MUST declare realistic values in `EngineRequirements`:

1. **`internalPersistentBytes` (Static Heap Retention):**
   - The total DRAM allocated during `initialize()` and retained across frames while the engine is in memory (e.g. data structures, caches, state objects, font descriptors).
   - *Example:* `MatrixRainEngine` retains ~1 KB of drop state arrays; `WeatherEngine` retains ~6 KB of weather model data and provider instances.
2. **`internalContiguousBytes` (Peak Contiguous Allocation):**
   - The largest single contiguous memory block required by the engine during runtime or initialization (e.g. decompression buffers, scratch arrays, TLS record payload).
   - *Example:* An engine decoding JPEG icons needs `~4 KB` contiguous RAM for decoder state; TLS engines require at least `16,000 B` to satisfy the mbedTLS incoming record buffer.
3. **`psramBytes` (Dedicated Working Buffer in SPIRAM):**
   - External RAM required for high-resolution offscreen canvases, sound effect samples, or large sprite sheets.
4. **`shadowBytesPerFrame` (Transient Allocations):**
   - Must be `0` for all standard engines. Any non-zero value represents transient heap allocations per frame, which are strictly prohibited on the Core 1 hot-path (Invariant 1).
5. **The Critical Role of `needsTls` in Memory Allocation:**
   - Setting `needsTls = true` informs `PipelineSelectionPolicy` that the engine will execute HTTPS handshakes. On classic ESP32 without PSRAM, this flag instructs the runtime to dynamically downgrade the HUB75 DMA presentation depth from **8-bit to 4-bit**, reclaiming **18 KB of DMA RAM** and exposing **50–64 KB of contiguous DRAM** (`NetworkBudget::TLS_MIN_LARGEST_BLOCK = 16,717 B`). This guarantees 100% reliable TLS handshakes without out-of-memory crashes.

---

### 4.2 Allocation Prediction & Teardown-Then-Measure Sandbox Model

ArcadeMatrix V4 relies on `CompatibilityEvaluator` (`src/core/CompatibilityEvaluator.h`) as the **sole, centralized authority** for determining whether an engine is feasible on the active device:

- **Two Distinct Evaluation Modes:**
  * `EvaluationMode::ReferenceCapability`: Static qualification against the hardware profile under reference budget baseline (`ReferenceMemoryProfile`). Powers the WebUI Catalog (`/api/engines`) and safety gating in `POST /api/rotation` and `POST /api/instances`, completely decoupled from transient Core 1 memory pressure (such as GIF playback). Evaluates against the *requested pipeline* (`targetPipeline`).
  * `EvaluationMode::RuntimeAdmission`: Dynamic pre-allocation validation checking live volatile heap state before allocating heavy resources.
- **Adaptive Color Depth (`COLOR_DEPTH_AUTO = 0`):** `PipelineSelectionPolicy` dynamically evaluates the optimal HUB75 DMA color depth based on geometry, PSRAM availability, and incoming engine requirements (`EngineRequirements`). Candidates are evaluated from the highest quality (8-bit) downward ($8 \dots 2$) across all platforms, including classic ESP32 without PSRAM. For graphics engines (e.g. Clock, Date, Temp, Marquee, GIFs), classic ESP32 on 128×32 and 64×32 panels achieves full **8-bit color depth**. When an incoming engine requires TLS (`needsTls = true`), the pipeline mathematically scales down to **4-bit depth** (2 bits only when the admission math proves 4 bits cannot fit), freeing up to 16–24 KB of contiguous DRAM and ensuring 100% reliable TLS handshakes.
- **Teardown-Then-Measure Sandbox Model in `RotationManager`:** Transitions execute in a deterministic sequence:
  1. `oldEngine->deactivate()`: Triggers Core 1 non-blocking logical quiescence and scoped socket abort.
  2. `maybeReconfigurePipelineFor(newEngine)`: Evaluates available headroom under complete Output Enable (OE) hardware blanking ($< 30\text{ ms}$). When switching pipelines on classic ESP32 without PSRAM, tearing down the previous engine deallocates canvas and DMA buffers (`panelReleased == true`), exposing the clean ~72 KB baseline sandbox memory pool (0-byte leak across rotations).
  3. Dynamic bit-depth selection ($8 \dots 2$) ensures TLS engines check `NetworkBudget::TLS_MIN_LARGEST_BLOCK` (16,717 B) on this clean released zone, achieving deterministic 4-bit admission and eliminating depth flapping.
  4. `newEngine->activate()`: Instantiates with maximum available memory.
  5. Output Enable (OE) is released (LOW) strictly after Frame 0 is rendered and committed (`firstFrameCommitted == true`). If target and progressive fallback allocations fail, OE remains HIGH (`PresentationRecovery`).
- **Early Boot Heap Consolidation (Step 3c):** `WebServerAPI` and its background `async_tcp` task (8 KB stack) are pre-initialized immediately following WiFi driver pre-init on Core 0, anchoring the stack down at the heap baseline (`0x3ffe4d20`) before HUB75 DMA allocations. This permanently eliminates the SRAM 1 "concrete pillar" (`0x3fff3d70`) that previously bisected contiguous memory.
- **Declarative HTTP Concurrency:** The firmware advertises `capabilities.http.recommendedConcurrency` (1 on `ESP32_STD`, 3 on `WAVESHARE_S3`). The frontend `HttpRequestQueue` bounds transport `fetch()` calls to this limit, preventing LwIP socket starvation while GIF or canvas operations run.
- **Statically Qualified Safe Fallback:** If dynamic memory allocation fails during transition `initialize(new)`, the runtime falls back to a statically qualified Safe Fallback engine requiring 0 PSRAM, 0 audio, 0 network, and $\le 2$ KB bounded RAM.

---

### 4.3 Engine Retirement & Destruction Pipeline (Core 1 vs Core 0)

To guarantee 60 FPS presentation without micro-stuttering while preventing Use-After-Free (UAF) crashes and memory leaks, ArcadeMatrix enforces a strict **Two-Stage Lifecycle Separation** (Invariants 15 & 16):

```mermaid
sequenceDiagram
    autonumber
    participant Core1 as Core 1 (Render Hot-Path)
    participant RM as RotationManager
    participant Queue as EngineRetirementQueue (SPSC)
    participant Core0 as Core 0 (Lifecycle0 Task)
    participant Engine as Engine Instance

    Note over Core1,RM: Stage 1: Logical Quiescence (Core 1)
    RM->>Engine: deactivate() [Non-blocking, O(1), abort sockets]
    RM->>RM: Clear currentActiveInstanceId
    RM->>RM: DisplayRuntime::purgeEngineReferences()
    RM->>Engine: setResourceState(CORE1_RELEASED)
    RM->>Queue: retire(std::move(engineUniquePtr))

    Note over Queue,Core0: Stage 2: Physical Quiescence & Destruction (Core 0)
    Queue-->>Core0: Pop engineUniquePtr
    Core0->>Engine: shutdownForDestruction() [Cooperative wait <= 300ms]
    alt Succeeded (Workers exited cleanly)
        Core0->>Engine: setResourceState(RETIRED)
        Core0->>Engine: delete engine (Reclaim DRAM / DMA)
    else Timeout (> 300ms)
        Core0->>Engine: setResourceState(QUARANTINED)
        Note over Core0: Quarantine pool retains pointer (Safe bounded leak > UAF crash)
    end
```

#### 1. Stage 1: Logical Quiescence on Core 1 (`deactivate()`)
- **Execution Context:** Core 1 render thread.
- **Contract:** Must be $O(1)$, non-blocking, zero task delays (`vTaskDelay`), zero mutex acquisition, zero dynamic allocations.
- **Actions Required:**
  * Set internal running flags to `false` (`m_running.store(false)`).
  * Detach surface pointer (`surface = nullptr`).
  * Trigger immediate scoped socket abort: `net::SecureHttpClient::abortSessionsOwnedBy(ownerId)` (or `session.abort()`).
  * **Strict Prohibition:** NEVER block Core 1 waiting for FreeRTOS tasks to terminate or network sockets to close!

#### 2. Stage 2: Physical Quiescence & Destruction on Core 0 (`shutdownForDestruction()`)
- **Execution Context:** Core 0 background `Lifecycle0` task (`Core0LifecycleDispatcher`).
- **Contract:** Executes asynchronously after the engine has been retired from Core 1.
- **Actions Required:**
  * Request background worker tasks to exit cooperatively (`m_stopWorker.store(true)`).
  * Wait cooperatively in short slices (`vTaskDelay(pdMS_TO_TICKS(10))`) up to a strict timeout (e.g. 300 ms).
  * Close open file handles, release DMA ring buffers, delete FreeRTOS queues.
  * Return `true` if all worker tasks exited cleanly and resources are quiescent.
  * Return `false` if cooperative exit timed out.
- **Strict Prohibition on Forced Kill:** DO NOT call `vTaskDelete(taskHandle)` forcibly on a running task! If a task is forcibly killed while executing inside mbedTLS or lwIP, internal locks remain held, memory structures corrupt, and the system crashes.

#### 3. The 6-Step Anti-UAF Release Barrier in `RotationManager::retireEngineSlot()`
When an engine slot is replaced or removed during rotation, `RotationManager` executes the formal 6-step release barrier:
1. `eng->deactivate()`: Signals logical quiescence and socket abort.
2. Clear `currentActiveInstanceId` if matching.
3. `DisplayRuntime::purgeEngineReferences(eng, instId)`: Purges any dangling pointers from runtime session and the preemption stack.
4. `eng->setResourceState(EngineResourceState::CORE1_RELEASED)`: Atomic state transition.
5. Sever local slot `instanceId`: Engine can never be looked up again by `findActiveEngine()`.
6. Move `std::unique_ptr<IEngine>` into `EngineRetirementQueue` for Core 0 dispatch.

#### 4. The Quarantine Mechanism (Safe Leak > Use-After-Free)
If `shutdownForDestruction()` returns `false` (worker task stuck or unresponsive within 300 ms):
- The engine enters `EngineResourceState::QUARANTINED`.
- It is placed into a bounded quarantine array on Core 0. The dispatcher periodically retries `shutdownForDestruction()`.
- If the quarantine capacity (8 engines) is reached, the pointer is preserved (`engine.release()`).
- **Architectural Guarantee:** A bounded, controlled memory leak is infinitely preferable to memory corruption or a Use-After-Free crash.

#### 5. Destructor Safety Barrier (`~MyEngine()`)
The C++ destructor MUST provide a final safety barrier:
```cpp
MyEngine::~MyEngine() {
    // Anti-UAF Safety Barrier:
    // In normal operation, shutdownForDestruction() on Core 0 has already stopped workers.
    // If deleted directly or quarantined destruction was bypassed, guarantee worker exit.
    if (m_workerTask && !m_workerExited.load(std::memory_order_acquire)) {
        m_stopWorker.store(true, std::memory_order_release);
        net::SecureHttpClient::abortSessionsOwnedBy(OWNER_MY_ENGINE);
        while (!m_workerExited.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        m_workerTask = nullptr;
    }
    // Free dynamic buffers via swap or delete
    std::vector<MyItem>().swap(m_items);
}
```

> [!IMPORTANT]
> **Mandatory Workflow When Adding a New Engine:**
> 1. Declare all resource constraints accurately in `EngineRequirements` in your engine descriptor.
> 2. Add your engine's descriptor to `getCanonicalEngineDescriptors()` in `test/native/tools/matrix_generator.cpp`.
> 3. Run `rtk python3 scripts/generate_engine_matrix.py` to regenerate [docs/ENGINE_COMPATIBILITY_MATRIX.md](ENGINE_COMPATIBILITY_MATRIX.md).
> 4. Verify CI pass with `rtk python3 scripts/validate_docs.py` (which runs `generate_engine_matrix.py --check`).

---

## 5. The ConfigSchema & ConfigField Reference

The schema is the **single source of truth** for both the WebUI form generator and the `ConfigSanitizer`:

```cpp
struct ConfigField {
    String id;                          // Key in config.json
    ConfigType type;                    // BOOLEAN, INTEGER, FLOAT, STRING, ENUM, COLOR, LIST
    String label;                       // WebUI label
    String description;                 // Tooltip text
    String default_value;               // Injected by sanitizer if missing
    bool required = false;
    String min_val = "";                // Numeric lower bound
    String max_val = "";                // Numeric upper bound
    String step = "";                   // UI stepper granularity
    String options = "";                // Comma-separated static choices
    String visible_when = "";           // Conditional visibility rule
    String options_endpoint = "";       // Dynamic choices endpoint
    bool multiple = false;              // Multiselect flag
    ValidationPolicy validation_policy; // Clamp, FallbackDefault, Reject, Accept
};
```

---

## 6. Custom / Dynamic Option Lists (`options_endpoint`)

When an engine's selectable options are generated dynamically (e.g. clock themes, SD fonts, GIF playlists), set `options_endpoint`:

```cpp
{
    .id = "theme",
    .type = ConfigType::ENUM,
    .label = "Clock Theme",
    .description = "Select pixel-art background theme",
    .default_value = "12",
    .options_endpoint = "/api/themes"
}
```

The WebUI queries `GET /api/themes` asynchronously and populates the `<select>` dropdown.

---

## 7. Multiselect Fields

To allow users to select multiple options (stored as a comma-separated string):

```cpp
{
    .id = "playlists",
    .type = ConfigType::LIST,
    .label = "Active Playlists",
    .description = "Select GIF folders to cycle through",
    .default_value = "arcade,retro",
    .options_endpoint = "/api/playlists",
    .multiple = true
}
```

---

## 8. Conditional Fields (`visible_when`)

Hide or show fields depending on another field's value:

```cpp
{
    .id = "custom_color",
    .type = ConfigType::COLOR,
    .label = "Custom Accent Color",
    .default_value = "#ff0055",
    .visible_when = "theme=20" // Only visible when Custom Gradient theme (20) is selected
}
```

---

## 9. Self-Healing Validation Policies

| Policy | Behavior on Out-of-Bounds Value |
| :-- | :-- |
| `ValidationPolicy::Clamp` | Restricts value to `[min_val, max_val]`. |
| `ValidationPolicy::FallbackDefault` | Resets invalid value to `default_value`. |
| `ValidationPolicy::Accept` | Accepts value as-is. |
| `ValidationPolicy::Reject` | Leaves field unmodified. |

---

## 10. Tutorial: Create a New Engine Step-by-Step

### Step 1: Create Header `src/engines/MatrixRainEngine.h`

```cpp
#pragma once
#include <Arduino.h>
#include "core/EngineContract.h"
#include "core/drawing/IDrawingSurface.h"

class MatrixRainEngine : public IEngine {
public:
    MatrixRainEngine();
    ~MatrixRainEngine() override;

    // --- Core 1 Lifecycle Hooks ---
    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override; // Non-blocking logical quiescence on Core 1

    // --- Core 0 Lifecycle Destruction Hook ---
    bool shutdownForDestruction() override; // Physical quiescence on Core 0

    // --- Dynamic Configuration & Framerate ---
    void onConfigChanged(const EngineConfig* config) override;
    bool isRealtime() const override { return true; }

private:
    IDrawingSurface* surface = nullptr;
    int speed = 2;
    int dropY[128];
};
```

### Step 2: Implement Logic `src/engines/MatrixRainEngine.cpp`

```cpp
#include "MatrixRainEngine.h"

MatrixRainEngine::MatrixRainEngine() {
    memset(dropY, 0, sizeof(dropY));
}

MatrixRainEngine::~MatrixRainEngine() {
    // Destructor safety barrier: ensure surface detached and state clean
    surface = nullptr;
}

EngineError MatrixRainEngine::initialize(EngineContext* context, const EngineConfig* config) {
    if (!context || !context->getSurface()) return EngineError::InitializationFailed;
    surface = context->getSurface();
    if (config) speed = config->getInt("speed", 2);
    return EngineError::OK;
}

void MatrixRainEngine::activate() {
    for (int i = 0; i < 128; i++) dropY[i] = random(-32, 0);
}

void MatrixRainEngine::update(EngineContext* context) {
    if (!surface) return;
    for (int x = 0; x < surface->width(); x += 4) {
        dropY[x] += speed;
        if (dropY[x] > surface->height()) dropY[x] = random(-16, 0);
    }
}

void MatrixRainEngine::render(EngineContext* context) {
    if (!surface) return;
    surface->fillScreen(0);
    for (int x = 0; x < surface->width(); x += 4) {
        surface->drawPixel(x, dropY[x], IDrawingSurface::color565(0, 255, 70));
    }
}

void MatrixRainEngine::deactivate() {
    // Stage 1 (Core 1): Non-blocking logical quiescence. Detach surface immediately.
    surface = nullptr;
}

bool MatrixRainEngine::shutdownForDestruction() {
    // Stage 2 (Core 0): Physical quiescence.
    // MatrixRain has no background worker tasks or open sockets, so destruction is immediately safe.
    return true;
}

void MatrixRainEngine::onConfigChanged(const EngineConfig* config) {
    if (config) speed = config->getInt("speed", 2);
}
```

### Step 3: Implement `IEngineDescriptorHandler` with Accurate Requirements

In your engine file (e.g. `src/engines/MatrixRainEngine.h` / `.cpp`):
```cpp
class MatrixRainEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override {
        EngineDescriptor desc;
        desc.metadata = { "matrix_rain", "Matrix Digital Rain", "animations", FIRMWARE_VERSION };
        desc.capabilities = {
            .supports_128x32 = true,
            .supports_256x64 = true,
            .realtime = true,
            .interruptible = true,
            .allowsOverlay = true,
            .allowRotation = true
        };
        // Realistic memory modeling for deterministic allocation prediction
        desc.requirements.needsPsram = false;
        desc.requirements.needsAudio = false;
        desc.requirements.needsNetwork = false;
        desc.requirements.needsTls = false;
        desc.requirements.targetFps = 60;
        desc.requirements.supportsSingleBuffer = true;
        desc.requirements.prefersDoubleBuffer = true;
        desc.requirements.internalPersistentBytes = 1024;    // 128 ints + state
        desc.requirements.internalContiguousBytes = 2048;    // Scratch headroom

        desc.schema.fields = {
            ConfigField("speed", ConfigType::INTEGER, "Fall Speed", "Falling speed in pixels per frame", "2", false, "1", "5", "1", "", "", false, "", ValidationPolicy::Clamp)
        };
        desc.factory = []() { return std::unique_ptr<IEngine>(new MatrixRainEngine()); };
        return desc;
    }
};
```

### Step 4: Register in `EngineRegistrar.cpp` and `matrix_generator.cpp`

1. **Register on Target Hardware (`src/engines/EngineRegistrar.cpp`):**
```cpp
#include "MatrixRainEngine.h"

void EngineRegistrar::registerAll() {
    // ...
    static const MatrixRainEngineDescriptorHandler matrixRainHandler;

    const IEngineDescriptorHandler* handlers[] = {
        // ...
        &matrixRainHandler
    };

    for (const auto* handler : handlers) {
        if (handler) registerHandler(*handler);
    }
}
```

2. **Register in Native Verification Suite (`test/native/tools/matrix_generator.cpp`):**
To ensure CI and matrix generation validate your new engine against all 5 reference profiles, add its descriptor to `getCanonicalEngineDescriptors()`:
```cpp
    // Matrix Rain
    {
        EngineDescriptor d;
        d.metadata = {"matrix_rain", "Matrix Digital Rain", "animations", FIRMWARE_VERSION};
        d.requirements.targetFps = 60;
        d.requirements.prefersDoubleBuffer = true;
        d.requirements.supportsSingleBuffer = true;
        d.requirements.internalPersistentBytes = 1024;
        d.requirements.internalContiguousBytes = 2048;
        engines.push_back(d);
    }
```

### Step 5: Regenerate Engine Compatibility Matrix & Validate CI

After registering the descriptor, regenerate the documentation matrix and run validation:
```bash
# 1. Regenerate Markdown compatibility matrix
rtk python3 scripts/generate_engine_matrix.py

# 2. Run documentation and architecture guard checks
rtk python3 scripts/validate_docs.py
```

---

### 10.1 Advanced Pattern: Network Engine with Background Worker & TLS

Engines performing network requests and background polling (e.g. market data, weather forecasts) MUST implement asynchronous task coordination, keep-alive batching, and strict anti-UAF destruction:

```cpp
// --- Header Pattern (e.g. MyNetworkEngine.h) ---
class MyNetworkEngine : public IEngine {
public:
    MyNetworkEngine();
    ~MyNetworkEngine() override;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;                 // Core 1 non-blocking abort
    bool shutdownForDestruction() override;     // Core 0 cooperative wait <= 300ms

private:
    static void workerTaskEntry(void* arg);
    void fetchQuotes();

    TaskHandle_t m_workerTask = nullptr;
    std::atomic<bool> m_stopWorker{false};
    std::atomic<bool> m_workerExited{true};
    IDrawingSurface* surface = nullptr;
};
```

```cpp
// --- Implementation Pattern (e.g. MyNetworkEngine.cpp) ---
EngineError MyNetworkEngine::initialize(EngineContext* context, const EngineConfig* config) {
    surface = context ? context->getSurface() : nullptr;
    if (!surface) return EngineError::InitializationFailed;

    // Start background polling worker pinned to Core 0
    m_stopWorker.store(false, std::memory_order_relaxed);
    m_workerExited.store(false, std::memory_order_relaxed);
    BaseType_t ret = xTaskCreatePinnedToCore(
        workerTaskEntry, "NetWorker", 4096, this, 1, &m_workerTask, 0 // Core 0
    );
    return (ret == pdPASS) ? EngineError::OK : EngineError::InitializationFailed;
}

void MyNetworkEngine::deactivate() {
    // Stage 1 (Core 1): Strictly non-blocking!
    // 1. Signal worker to stop
    m_stopWorker.store(true, std::memory_order_release);
    // 2. Abort all in-flight TLS/TCP sockets immediately (unblocks recv/connect)
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_MY_NETWORK);
    // 3. Detach surface
    surface = nullptr;
}

bool MyNetworkEngine::shutdownForDestruction() {
    // Stage 2 (Core 0): Cooperative wait up to 300 ms
    if (!m_workerTask) return true;

    m_stopWorker.store(true, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_MY_NETWORK);

    for (int i = 0; i < 30 && !m_workerExited.load(std::memory_order_acquire); i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (m_workerExited.load(std::memory_order_acquire)) {
        m_workerTask = nullptr;
        return true; // Clean exit! Safe to delete on Core 0.
    }

    // Task timed out: NEVER call vTaskDelete()! Return false to trigger quarantine.
    LOGW("MyNetworkEngine", "Worker did not exit in 300ms; engine will be quarantined.");
    return false;
}

MyNetworkEngine::~MyNetworkEngine() {
    // Destructor Safety Barrier: guarantee worker is dead before freeing buffers
    if (m_workerTask && !m_workerExited.load(std::memory_order_acquire)) {
        m_stopWorker.store(true, std::memory_order_release);
        net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_MY_NETWORK);
        while (!m_workerExited.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        m_workerTask = nullptr;
    }
}

void MyNetworkEngine::workerTaskEntry(void* arg) {
    auto* self = static_cast<MyNetworkEngine*>(arg);
    while (!self->m_stopWorker.load(std::memory_order_acquire)) {
        self->fetchQuotes();
        // Sleep in small slices to detect stop signal immediately
        for (int i = 0; i < 600 && !self->m_stopWorker.load(std::memory_order_acquire); i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    self->m_workerExited.store(true, std::memory_order_release);
    vTaskDelete(NULL); // Worker exits cleanly on its own
}
```

#### Network Engine Design Rules:
1. **Declare `needsTls = true`:** Automatically enables 4-bit presentation depth on classic ESP32, reclaiming 18 KB of DMA RAM.
2. **Reuse Keep-Alive Sessions (Golden Rule #19):** Use `net::SecureHttpSession` to execute multi-request batches with **one TLS handshake**.
3. **Use `IconService` + `JPEGDEC` (Golden Rule #20):** NEVER instantiate `new PNG()` on classic ESP32 (34 KB memory footprint triggers crash). Use `IconService` with proxy transcoding to JPEG decoded in ~2.5 KB RAM.
4. **Defer Polling During Active Presentation (Golden Rule #21):** On non-PSRAM devices, avoid network polling during active 4-bit presentation if cached data is present.

---

## 11. Tutorial: Add a Custom-List Endpoint

To serve dynamic options to the WebUI, register a route in `src/api/WebServerAPI.cpp`:

```cpp
server.on("/api/my_options", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    JsonArray arr = doc.to<JsonArray>();
    
    JsonObject opt1 = arr.createNestedObject();
    opt1["id"] = "opt_a";
    opt1["name"] = "Option Alpha";
    
    JsonObject opt2 = arr.createNestedObject();
    opt2["id"] = "opt_b";
    opt2["name"] = "Option Beta";

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
});
```

---

## 12. Tutorial: Add a New Clock Face / Theme Step-by-Step

Clocks in ArcadeMatrix are organized into modular `ClockFace` implementations managed by the core `ClockEngine`. To add a new visual theme or clock animation (e.g. *SpaceInvadersClock*):

### Step 1: Create `src/engines/clocks/SpaceInvadersClock.h` & `.cpp`

Inherit from the `ClockFace` base class (`src/engines/ClockEngine.h`):

```cpp
// src/engines/clocks/SpaceInvadersClock.h
#pragma once
#include "../ClockEngine.h"
#include "../../core/drawing/IDrawingSurface.h"

class SpaceInvadersClock : public ClockFace {
public:
    SpaceInvadersClock(IDrawingSurface* display, const EngineConfig* config = nullptr);
    void draw(const TimeData& t) override;
    void update() override;

private:
    int invaderFrame = 0;
    unsigned long lastAnimMs = 0;
};
```

```cpp
// src/engines/clocks/SpaceInvadersClock.cpp
#include "SpaceInvadersClock.h"

SpaceInvadersClock::SpaceInvadersClock(IDrawingSurface* display, const EngineConfig* config)
    : ClockFace(display, config) {}

void SpaceInvadersClock::update() {
    if (millis() - lastAnimMs > 500) {
        invaderFrame = (invaderFrame + 1) % 2;
        lastAnimMs = millis();
    }
}

void SpaceInvadersClock::draw(const TimeData& t) {
    if (!matrix) return;
    matrix->fillScreen(0);
    // Draw animated invaders & formatted time digits
    matrix->setTextSize(1);
    matrix->setTextColor(matrix->color565(0, 255, 100));
    matrix->setCursor(24, 12);
    matrix->printf("%02d:%02d:%02d", t.hour, t.minute, t.second);
}
```

### Step 2: Add Theme Enum ID in `src/engines/DateEngine.h`

Add your new theme identifier to `PublisherTheme`:

```cpp
enum PublisherTheme {
    // ... existing themes (0 to 24)
    THEME_SPACE_INVADERS = 25
};
```

### Step 3: Wire into `ClockEngine::setTheme()` in `src/engines/ClockEngine.cpp`

Include your header and instantiate your `ClockFace`:

```cpp
#include "clocks/SpaceInvadersClock.h"

// In ClockEngine::setTheme():
case THEME_SPACE_INVADERS:
    activeFace = new SpaceInvadersClock(legacy_matrix, config);
    break;
```

### Step 4: Expose Theme in `scripts/extract_engine_catalog.py`

Add your theme to `CANONICAL_THEMES` in `scripts/extract_engine_catalog.py` so it is automatically pre-compiled into the WebUI at build time:

```python
CANONICAL_THEMES = [
    # ...
    {"id": 25, "name": "Space Invaders Clock"},
]
```

The WebUI will automatically embed "Space Invaders Clock" in the theme dropdown at compile time (with zero RAM overhead on the ESP32), persist it in `config.json`, and apply it live via hot reload.

---

## 13. Internationalization & Centralized i18n (Front & Back)

ArcadeMatrix features a **fully centralized i18n architecture**.

> [!IMPORTANT]
> **Golden Rule: Never add a `lang` field to your engine's `ConfigSchema`.**
> Language is a global system setting (`system.lang`), chosen by the user in the WebUI header selector (`#lang-selector`). Any language change in the UI automatically sends a `POST /api/system` call and notifies all active engines in real time.

### A. Usage in C++ Engine (`#include "core/I18n.h"`)

All localized strings (weather day labels, weather conditions, text clock words, noise levels, etc.) are centralized in the `I18n` helper class:

```cpp
#include "core/I18n.h"

// 1. Get active language (FR, EN, ES)
Lang currentLang = I18n::getLang();

// 2. Weather day names (e.g., "TODAY", "TOM.", "MON"..)
const char* dayLabel = I18n::getWeatherDayLabel(dayOfWeek, isToday, isTomorrow);

// 3. Translated weather condition strings
String condition = I18n::getWeatherCondition("Thunderstorm with heavy rain");

// 4. WordClock full text lines
std::vector<String> lines = I18n::getWordClockLines(hours, minutes);

// 5. Noise / Decibel level statuses
const char* noise = I18n::getNoiseLevelLabel(levelIndex);
```

### B. Tutorial: Adding a New Language (e.g. German `de`) in 3 Steps

1. **Front-end WebUI (`data/index.html` or `i18n.js`):**
   Add the language code and label to `SUPPORTED_LANGUAGES` and provide translations in `translations`:
   ```javascript
   const SUPPORTED_LANGUAGES = [
     { code: 'fr', label: 'Français' },
     { code: 'en', label: 'English' },
     { code: 'es', label: 'Español' },
     { code: 'de', label: 'Deutsch' }
   ];
   ```
2. **ESP32 Back-end (`src/core/I18n.h` & `src/core/I18n.cpp`):**
   - Add `DE` to the `Lang` enum.
   - Implement localized day labels, conditions, WordClock words, and noise strings in `I18n.cpp`.
3. **Raspberry Pi Back-end (`src/core/i18n.rs`):**
   - Add `De` to `Lang` enum and provide mappings in the lookup tables.

---

## 14. Reading Config in an Engine

Engines receive an `EngineConfig` proxy:

```cpp
int speed = config->getInt("speed", 2);
String text = config->getString("title", "Arcade");
bool enabled = config->getBool("enabled", true);
float offset = config->getFloat("temp_offset", 0.0f);
```

---

## 15. Rendering into the LED Matrix & Responsive Geometry

ArcadeMatrix v4 abstracts display rendering behind the hardware-agnostic `IDrawingSurface` interface (which extends `Adafruit_GFX`). Always obtain the drawing surface pointer via `context->getSurface()`:

```cpp
IDrawingSurface* surface = context->getSurface();
surface->drawPixel(x, y, surface->color565(r, g, b));
surface->fillRect(x, y, w, h, color);
surface->setCursor(x, y);
surface->print("TEXT");

// Or high-performance block blit for streaming animations (GIFs, fighters):
surface->blit565(canvasBuffer, width, height);
```
*(For backwards compatibility, `context->getMatrix()` is preserved as a shim returning `MatrixPanel_I2S_DMA*`).
*Never call `flipDMABuffer()` inside an engine — the main display loop handles presentation centrally.*

### 15.1 The Golden Rule for Multi-Resolution & TATE Responsive Layouts

ArcadeMatrix displays can run in any resolution and orientation (`64x32`, `128x32`, `256x64`, `64x64`, `32x64`, `32x128`, `64x128`, `64x256`).

> [!IMPORTANT]
> **🏆 The Golden Rule of Engine Rendering:**
> 1. **Renderers must NEVER branch on `LayoutClass` or `if (w == 64 && h == 128)` directly.**
> 2. Create a companion pure `*LayoutCalculator` (e.g. `MyEngineLayoutCalculator::calculate(geometry)`) that produces a `MyEngineLayout` containing bounded `Rect`s.
> 3. The `render()` method draws exclusively into the provided `Rect`s.

#### Example: Responsive Music Engine
```cpp
// 1. Define bounded layout rectangles
struct MusicLayout {
    Rect artworkRect;
    Rect metadataRect;
    Rect progressRect;
    Rect visualizerRect;
};

// 2. Pure layout calculator
class MusicLayoutCalculator {
public:
    static MusicLayout calculate(const DisplayGeometry& geometry) {
        MusicLayout layout;
        if (geometry.layoutClass == LayoutClass::PORTRAIT || geometry.layoutClass == LayoutClass::TALL) {
            // Stack vertically: Artwork on top, metadata in middle, visualizer at bottom
            layout.artworkRect = { 2, 2, (uint16_t)(geometry.width - 4), (uint16_t)min((int)geometry.width - 4, (int)(geometry.height * 0.35f)) };
            layout.metadataRect = { 2, (int16_t)(layout.artworkRect.y + layout.artworkRect.height + 2), (uint16_t)(geometry.width - 4), 16 };
            layout.progressRect = { 2, (int16_t)(layout.metadataRect.y + 18), (uint16_t)(geometry.width - 4), 3 };
            layout.visualizerRect = { 2, (int16_t)(geometry.height - 12), (uint16_t)(geometry.width - 4), 10 };
        } else {
            // Landscape: Artwork on left, metadata and visualizer on right
            layout.artworkRect = { 2, 2, (uint16_t)(geometry.height - 4), (uint16_t)(geometry.height - 4) };
            layout.metadataRect = { (int16_t)(layout.artworkRect.width + 6), 2, (uint16_t)(geometry.width - layout.artworkRect.width - 8), 12 };
            layout.progressRect = { (int16_t)(layout.artworkRect.width + 6), 16, (uint16_t)(geometry.width - layout.artworkRect.width - 8), 2 };
            layout.visualizerRect = { (int16_t)(layout.artworkRect.width + 6), (int16_t)(geometry.height - 10), (uint16_t)(geometry.width - layout.artworkRect.width - 8), 8 };
        }
        return layout;
    }
};

// 3. Renderer consumes purely pre-bounded Rects
void MusicEngine::render(EngineContext* context) {
    MusicLayout layout = MusicLayoutCalculator::calculate(context->getGeometry());
    renderArtwork(layout.artworkRect);
    renderMetadata(layout.metadataRect);
    renderProgress(layout.progressRect);
    renderVisualizer(layout.visualizerRect);
}
```

#### Rebuilding Geometry-Derived Caches
Only implement `onDisplayGeometryChanged(const DisplayGeometry& geometry)` if your engine allocates fixed column counts, FFT arrays, or target grids (e.g. `MatrixRainClock`, `TetrisClock`, `VisualizerEngine`). Reallocate or adjust your caches non-destructively without resetting gameplay, scores, or timers.

### 15.2 Full-Motion Video, Canvas Streaming & FastBlit (`blitCanvas565`)

On high-resolution panels (`256x64`) utilizing PSRAM-resident DMA buffers, per-pixel writes (`drawPixel()`) incur read-modify-write operations across multiple bitplanes, capping full-frame animation throughput to 7–14 FPS.

For full-frame animation engines (e.g. video clips, scrolling arcade sequences):
1. **Render into a 16-bit RGB565 memory canvas buffer in PSRAM** (`uint16_t* canvasBuffer`).
2. **Stream via FastBlit:** Call `matrixEngine.blitCanvas565(canvasBuffer, width, height)`. This executes row-burst sequential DMA word writes directly into the back-buffer plane words, bypassing per-pixel overhead and completing a 256×64 frame copy in **~26 ms** (solid 30–33+ FPS).
3. **Opt out of overlays:** If your engine requires maximum frame throughput, override `bool allowsOverlay() const override { return false; }`.

### 15.3 Overlays vs Background Canvas (Transparent Compositing)

Overlays (such as `FighterEngine`) composite dynamically over the active engine:
- **Never erase with opaque rectangles:** Do **not** call `fillRect(..., 0)` to clear old sprite bounding boxes. The underlying engine (e.g. `TetrisClock`) already redraws its frame every tick. Calling `fillRect(..., 0)` will punch black holes into the background digits and canvas.
- **Draw strictly transparently:** Inspect pixel colors before drawing (`if (color != anim->transparentColor) matrix->drawPixel(...)`).
- **Screen clearing (`fillScreen(0)`):** When an engine clears its background, `FastMatrixPanel::fillScreen(0)` automatically clears only the color bits (`BITMASK_RGB12_CLEAR`) on the inactive back buffer. It never touches the active scanning front buffer, preventing horizontal scanline flicker.

## 16. Testing, QEMU Emulation & Local Compilation

ArcadeMatrix adheres to a strict test-driven development workflow. All core lifecycle methods, configuration sanitizers, and atomic concurrency state-machines are covered by automated tests.

### 16.1 Compiling Dual Firmware Targets

Ensure all changes build cleanly on both target hardware platforms:

```bash
# Standard ESP32 Dual-Core (I2S DMA)
rtk pio run -e esp32dev

# Waveshare ESP32-S3 (LCD DMA)
rtk pio run -e esp32s3_waveshare
```

### 16.2 Running Local Unit Tests (Compilation Tier)

Compile and validate test suites locally via PlatformIO:

```bash
# Compile all 8 unit test suites without physical board attached
rtk pio test -e esp32dev --without-uploading --without-testing

# Compile a specific test suite (e.g. test_core)
rtk pio test -e esp32dev -f test_core --without-uploading --without-testing
```

### 16.3 Running QEMU Hardware Emulation Tests

ArcadeMatrix includes an automated QEMU test runner (`scripts/run_qemu_tests.py`) that boots each test firmware image inside an emulated ESP32 CPU and evaluates the serial UART output:

```bash
# Run all 7 test suites inside QEMU emulator
rtk python3 scripts/run_qemu_tests.py

# Verbose output
rtk python3 scripts/run_qemu_tests.py -v
```

### 16.4 How to Write a New Unit Test

Unit tests use the `Unity` testing framework. Follow these conventions:

#### 1. Using `TrackingMockEngine` to Assert Lifecycle Calls
When testing `DisplayArbiter` or `DisplayRuntime`, use `TrackingMockEngine` to record lifecycle invocations:

```cpp
#include <unity.h>
#include "core/DisplayRuntime.h"

void test_my_custom_feature(void) {
    DisplayRuntime runtime;
    TrackingMockEngine mock("my_engine");

    runtime.registerSourceEngine(DisplaySourceId::MQTT, &mock, EngineHandle("my_engine", "inst1"));

    DisplayDecision decision;
    decision.valid = true;
    decision.sourceId = DisplaySourceId::MQTT;
    decision.engineHandle = EngineHandle("my_engine", "inst1");

    runtime.transitionSession(decision);

    TEST_ASSERT_EQUAL(1, mock.activateCalls);
    TEST_ASSERT_EQUAL(0, mock.pauseCalls);
    TEST_ASSERT_EQUAL(0, mock.deactivateCalls);
    TEST_ASSERT_EQUAL(TransitionMode::REPLACE, runtime.getCurrentSession().lastTransitionMode);
}
```

#### 2. Registering Tests in `setup()`
In your test suite file (e.g. `test/test_core/test_core.cpp`), register your test function inside `setup()`:

```cpp
void setup() {
    delay(1000);
    UNITY_BEGIN();
    RUN_TEST(test_my_custom_feature);
    UNITY_END();
}

void loop() {}
```

### 16.5 Documentation & Web Installer Validation Scripts

Run the static validation scripts to verify API sync, documentation tables, and web installer manifests:

```bash
# Validate documentation consistency across EN/FR/ES
rtk python3 scripts/validate_docs.py

# Validate web installer manifest
rtk python3 scripts/validate_webinstaller.py
```

---

## 17. Developer Checklist

### Architecture & Hot-Path (Core 1)
- [ ] `initialize()` allocates all persistent memory; hot loop (`update()` / `render()`) has **zero dynamic allocations** (`malloc`, `new`, `String`, vector growth).
- [ ] Core 1 rendering uses `IDrawingSurface` exclusively (zero direct hardware / DMA register access).
- [ ] Responsive multi-resolution layouts use pure `*LayoutCalculator` returning bounded `Rect`s (no inline resolution branching).
- [ ] `onConfigChanged()` updates state in place without destroying or recreating the instance.
- [ ] `deactivate()` is **strictly non-blocking and $O(1)$** on Core 1: detaches surface, aborts network sessions, sets atomic stop flags. Zero mutex locks, zero `vTaskDelay()`, zero memory allocations.

### Engine Destruction & Resource Reclamation (Core 0)
- [ ] Engines with background tasks implement `shutdownForDestruction()` executing cooperatively on Core 0.
- [ ] Cooperative shutdown waits in slices (`vTaskDelay(pdMS_TO_TICKS(10))`) up to 300 ms for tasks to exit.
- [ ] **Zero forced task kills:** `vTaskDelete(taskHandle)` is NEVER called forcibly; timed-out engines return `false` for quarantine.
- [ ] Destructor `~MyEngine()` implements the anti-UAF safety barrier to guarantee background tasks are dead before deleting member buffers.
- [ ] Memory buffers are released cleanly using RAII or swap idiom (`std::vector<T>().swap(vec)`).
- [ ] `deactivate()` clears all dynamic strings/buffers (`std::vector<T>().swap(vec)` or `String()`), leaving **zero survivors** in the Volatile Sandbox Zone.

### Memory Modeling & Allocation Prediction
- [ ] `EngineRequirements` declares realistic footprints:
  * `internalPersistentBytes`: DRAM retained across frames while in memory.
  * `internalContiguousBytes`: Peak contiguous allocation needed (scratch / decoding / TLS record buffer).
  * `shadowBytesPerFrame`: Must be `0` for hot-path engines.
- [ ] `needsTls` is set to `true` for any engine using HTTPS/TLS, signaling the runtime to adapt to 4-bit presentation depth on classic ESP32.
- [ ] Engine descriptor is registered in `src/engines/EngineRegistrar.cpp` AND in `test/native/tools/matrix_generator.cpp`.

### Network & Asset Optimization
- [ ] Remote data and chart engines implement `prefetchData()` using `fetchCombined()` keep-alive batching during the transition window before panel allocation.
- [ ] Network engines use `net::SecureHttpSession` keep-alive batching for multi-item requests (one TLS handshake per batch).
- [ ] All icons use `IconService` + `JPEGDEC` in ~2.5 KB RAM; dynamic `new PNG()` / `PNGdec` is **strictly avoided** on classic ESP32.
- [ ] Background network polling is deferred during active 4-bit presentation on non-PSRAM hardware.

### Localization & Validation
- [ ] Localized strings use the centralized `I18n` module (no hardcoded localized strings or redundant `lang` field in schema).
- [ ] `options_endpoint` is provided for dynamic options.
- [ ] Code compiles cleanly on both targets: `rtk pio run -e esp32dev -e esp32s3_waveshare`.
- [ ] Unit test suites pass: `rtk pio test -e esp32dev --without-uploading --without-testing`.
- [ ] Compatibility matrix regenerated: `rtk python3 scripts/generate_engine_matrix.py`.
- [ ] Documentation validation passes: `rtk python3 scripts/validate_docs.py`.
