# Memory Optimization & Reclamation Architecture: Achieving TLS & High-Fidelity Rendering on Constrained ESP32 Hardware

## 1. Executive Summary & The Core Challenge

The ESP32 classic (dual-core Xtensa LX6, non-S3) possesses approximately 320 KB of internal SRAM shared across instructions (IRAM), static data (.data / .bss), and general-purpose heap (DRAM). At the entry point of the application `setup()` function, `heap_caps_get_free_size(MALLOC_CAP_8BIT)` reports **~190.5 KB of initial free DRAM**, which quickly contracts to **~56 to 62 KB** once Wi-Fi MAC buffers, lwIP sockets, and the web server runtime are initialized.

Driving an LED matrix with HUB75 DMA introduces severe memory pressure:
* On a **128×32 matrix** (4,096 RGB LEDs), an 8-bit color depth requires up to **~36.5 KB of contiguous DMA RAM** (plus I2S descriptor ring buffers).
* An HTTPS request powered by **mbedTLS** (required for weather APIs, Spotify, crypto trackers, stock quotes, and RSS news) requires **~35 to 40 KB of free internal DRAM** during TLS handshake negotiation (SSL context, cipher state, and two 16,717 B input/output record buffers).
* Traditional rendering architectures (e.g. DMA double buffering) demand an additional 32 KB, immediately inducing an Out-Of-Memory (OOM) panic or heap fragmentation failure.

Through a systematic, layered optimization initiative, ArcadeMatrix reclaimed over **~24 KB of static DRAM** and **~30 KB of contiguous heap headroom**, establishing deterministic 60 FPS graphical rendering alongside reliable TLS operations on 128×32 panels without PSRAM.

---

## 2. Architectural Optimizations

### 2.1 Single DMA + Canvas Pipeline (`canvas_single`)

#### The Problem
Standard HUB75 display libraries allocate two complete DMA buffers (`display_dma_buffer` double-buffering) to prevent visual tearing, consuming $2 \times 32\,\text{KB} = 64\,\text{KB}$ on 128×32 @ 8-bit. This leaves virtually zero contiguous DRAM for any network task.

#### The Architectural Solution
ArcadeMatrix introduced the `canvas_single` pipeline:
1. **Single DMA Buffer:** A single physical DMA buffer scanned continuously by the ESP32 I2S parallel engine.
2. **Offscreen Canvas:** Engines render to an offscreen SRAM canvas (`CanvasBufferedSurface`).
3. **Transactional Presentation:** `IDrawingSurface::present()` transfers dirty rows from the canvas into the active DMA scan buffer within safe scanline windows, eliminating tearing while reclaiming an entire physical DMA frame buffer (~16 to 32 KB depending on color depth).
4. **DMA Isolation (Invariant 19):** Rendering engines never touch the physical DMA buffer directly, enforcing strict isolation through `IDrawingSurface` (see [ARCHITECTURE.md](ARCHITECTURE.md#232-formal-architectural-invariants-15-through-20)).

---

### 2.2 Dynamic Presentation Pipeline & Full Auto Color Maximizer ($8 \leftrightarrow 7 \dots 2$ Adaptive Depth)

#### The Problem
High-contrast graphics (GIFs, Street Fighter sprites, marquee banners, clock faces) look significantly richer in 8-bit color depth (16.7 million colors), but their DMA footprint starves mbedTLS on memory-constrained hardware (e.g. ESP32 classic 128×32 without PSRAM). Conversely, fixing color depth to 4 bits permanently degrades graphical fidelity 100% of the time, while setting a static 6-bit clamp unnecessarily limits capable graphics engines. Furthermore, a naive runtime upscale (e.g. jumping blindly from 4 bits back to 8 bits) risks an immediate Out-Of-Memory (OOM) panic if internal DRAM has fragmented during network operations.

#### The Architectural Solution
Rather than enforcing a static compromise at boot or a blind toggle, the **Dynamic Presentation Pipeline** combines hardware quiescence with a rigorous multi-dimensional predictive memory model in `PipelineSelectionPolicy::resolveTargetDepth`:

1. **Multi-Dimensional Mathematical Capacity Evaluation:**
   Between rotation slots (strictly after `oldEngine->deactivate()` achieves quiescence and before `newEngine->activate()` allocates), `DisplayRuntime` queries real-time internal DRAM and DMA headroom. Candidate depths $D \in [8 \dots 2]$ are evaluated from the highest quality downward against four mathematical bounds:
   * **Free Internal DRAM Headroom:**
     $$\widehat{F}(D) = \text{currentFreeInternalHeap} + (\text{currentDepth} - D) \times \text{bytesPerBit} \ge \text{SYSTEM\_MIN\_HEADROOM\_RESERVE} + \text{req.internalPersistentBytes} + \text{netReserve} + \text{audioReserve}$$
   * **Largest Contiguous DRAM Block:**
     $$\widehat{L}(D) = \text{currentLargestBlock} + (\text{currentDepth} - D) \times \text{bytesPerBit} \ge \text{minContiguousNeeded}$$
     where $\text{minContiguousNeeded} = \text{NetworkBudget::TLS\_MIN\_COMBINED\_BLOCK}$ ($28,672\,\text{bytes}$) for TLS engines.
   * **Internal DMA Capacity:**
     $$\widehat{Dma}(D) = \text{currentFreeDma} + (\text{currentDepth} - D) \times \text{bytesPerBit} \ge \text{minDmaNeeded}$$
     where $\text{minDmaNeeded} = \text{NetworkBudget::TLS\_MIN\_FREE\_DMA}$ ($16,384\,\text{bytes}$) for TLS hardware SHA acceleration.
   * **PSRAM Feasibility:** Verified for PSRAM-enabled targets (such as ESP32-S3), ensuring that the same mathematical capacity model protects all hardware targets.

2. **Continuous Dynamic Stepping ($8 \leftrightarrow 7 \leftrightarrow 6 \leftrightarrow 5 \leftrightarrow 4 \leftrightarrow 3 \leftrightarrow 2$):**
   * **TLS Network Engines (Crypto, Stock, Spotify, GNews, Weather):** Step down mathematically to the highest safe depth satisfying all bounds (typically 4 bits, or 2 bits under extreme pressure), releasing up to **16 to 24 KB of contiguous DRAM** immediately before mbedTLS negotiates.
   * **Graphical Engines (GIFs, Fighter, Clock, Canvas, Matrix, Marquee):** Evaluated from 8 bits downward, restoring full **8-bit color depth** whenever safe. On 128×32 and 64×32 panels without PSRAM, Clock and graphics engines run at full 8-bit depth!
   * **WebUI Integration:** When "Dynamic Presentation Pipeline" is enabled, the manual `Color Depth` dropdown is automatically disabled (grayed out) with an informative note explaining that depth is determined mathematically per rotation slot.

3. **Floor Protection (2-Bit Minimum):**
   `FastMatrixPanel::initLuts` and presentation reconfigure guards support down to 2-bit color depth ($2 \le \text{depth} \le 8$), ensuring an ultra-low memory operational fallback under extreme heap fragmentation without blackouts.

4. **Hardware Presentation Transaction (Invariant 21):**
   During the inter-engine transition window, the hardware transaction executes cleanly:
   1. `oldEngine->deactivate()` establishes non-blocking Core 1 logical rendering quiescence (detaching drawing surface). Background task/network quiescence is finalized by `shutdownForDestruction()` prior to pipeline teardown.
   2. `OE = HIGH` (Output Enable asserted: panel physically blanked in hardware).
   3. Teardown active I2S/LCD DMA pipeline.
   4. Attempt target DMA allocation (`requestedDepth`: up to 8 bits for graphics, 4 bits nominal for TLS).
   5. If target allocation fails, initiate Progressive Fallback Attempt ($4 \to 2$ bits).
   6. If all fallbacks fail, maintain `OE = HIGH` in `PresentationRecovery` (no corrupted visual output).
   7. Rebuild color LUTs via `FastMatrixPanel::initLuts(effectiveDepth)`.
   8. Commit Frame 0 (deterministic black frame) via `m_presentationBackend->commitFirstFrame()`.
   9. **Invariant P0:** `OE = LOW` (Output Enable released) strictly after `firstFrameCommitted == true`.
   * **Zero Glitch / Visual Invisibility:** The entire blackout window achieves a **< 30 ms qualification target**, completely imperceptible during engine transitions.
   * Telemetry rigorously tracks `requestedDepth`, `effectiveDepth`, `fallbackAttempted`, and `fallbackUsed = (effectiveDepth != requestedDepth)`.
   * Detailed formal rules and DMA sizing tables are maintained in [MEMORY_MODEL.md](MEMORY_MODEL.md).

---

### 2.3 Color Fidelity & Dynamic Quantization LUT Override (`FastMatrixPanel::initLuts`)

#### The Problem
The upstream `ESP32-HUB75-MatrixPanel-I2S-DMA` library is compiled with fixed 8-bit luminance conversion lookup tables (`lumConvTab_8bit`). When the color depth is reduced to 6, 5, or 4 bits at runtime, the base library's color mapping fails:
- Bitplane bitshifts overflow or misalign, causing color clipping, blown-out highlights, severe hue inversion, and posterization.
- The base library does not dynamically rebuild its internal quantization tables when depth changes post-boot.

#### The Architectural Solution
`FastMatrixPanel` (derived from `MatrixPanel_I2S_DMA` in `src/core/MatrixEngine.cpp`) completely overrides color conversion and bitplane dispatch:
1. **Dynamic LUT Generation (`FastMatrixPanel::initLuts(uint8_t depth)`):**
   ```cpp
   void FastMatrixPanel::initLuts(uint8_t depth) {
       uint8_t shift = 8 - depth;
       uint8_t round = (shift > 0) ? (1 << (shift - 1)) : 0;
       uint16_t maxVal = (1 << depth) - 1;
       // Precomputes m_lut_r[32], m_lut_g[64], m_lut_b[32] scaled from 8-bit luminance
   }
   ```
2. **Per-Channel Gamma & Rounding:** Scales the 8-bit gamma curves down to the target bit depth ($2 \le \text{depth} \le 8$) with proper rounding (`(val + round) >> shift`), preventing dark crush and hue distortion.
3. **Instant Runtime Recalibration:** Whenever the presentation pipeline reconfigures ($8 \leftrightarrow 4$ or $6 \leftrightarrow 4$), `initLuts(newDepth)` is immediately called, ensuring faithful and colorimetric reproduction without requiring an ESP32 reboot.

---

### 2.4 Strict Network Quiescence & Scoped Socket Abort

#### The Problem
If a network engine's slot ends while an HTTPS request is in-flight, lingering TCP sockets, half-open LwIP TCP buffers, or active worker threads retain mbedTLS allocations (~35-40 KB), preventing DMA reconfiguration or crashing the next engine.

#### The Architectural Solution
* **Formal Deactivation Protocol:** `oldEngine->deactivate()` executes a 5-step quiescence procedure:
  1. Signals background workers to stop via atomic flags (`m_stopFetch = true`).
  2. Forcibly aborts underlying HTTP/TLS transport via scoped session abort (`net::SecureHttpClient::abortSessionsOwnedBy(ownerId)`).
  3. Executes a bounded, deterministic wait for worker thread exit during Core 0 destruction (`shutdownForDestruction()`).
  4. Closes transport handles and cancels active descriptors.
  5. Purges transient JSON and quote caches using the `std::swap` deallocation idiom (Invariant 15, see [ARCHITECTURE.md](ARCHITECTURE.md#232-formal-architectural-invariants-15-through-20)).
* **Client-Side Abort:** Immediate socket abort terminates the socket descriptor in LwIP. LwIP automatically rejects any subsequent incoming server packets with a TCP RST without allocating DRAM buffers.
* **Invariant N8 (Post-Quiescence Application Isolation):** Once a session is aborted and its owning engine is quiescent, no further application processing, parsing, or allocation may occur.

---

### 2.5 Zero-Allocation HTTP Streaming & Fixed Stack Buffers

#### The Problem
During concurrent mbedTLS streaming, mbedTLS retains ~33.4 KB of internal DRAM for in/out record buffers. If REST API parsers dynamically reallocate heap memory via `std::vector::reserve(300)` while the largest free block is temporarily depressed ($< 8\,\text{KB}$), `operator new` throws `std::bad_alloc`, triggering an immediate system abort/crash on Core 1.

#### The Architectural Solution
* **Fixed Stack Allocation:** Replaced dynamic `std::vector` heap resizing in API response parsers with bounded, fixed-capacity stack arrays:
  ```cpp
  constexpr size_t MAX_RAW_PRICES = 320;
  float rawPrices[MAX_RAW_PRICES];
  ```
* **Zero Contention with mbedTLS:** REST responses stream into pre-allocated stack frames without touching the heap, guaranteeing zero `bad_alloc` panics even under peak network DRAM pressure.

---

### 2.6 Lazy Buffer Allocation (Marquee Raw Buffer & GIF Decoders)

#### The Problem
`MarqueeEngine` historically allocated an 8 KB contiguous raw RGB565 image buffer at startup to support icon display, even when running purely text-based marquee animations.

#### The Architectural Solution
* Converted the raw buffer into an **on-demand, lazy allocation**:
  - Initialized to `nullptr`.
  - Allocated only when an icon or image file is explicitly parsed.
  - Reclaimed immediately via `freeRawBuffer()` on text-only modes or when deactivated.
* GIF decode tables in `GifEngine` are allocated strictly on-demand during active playback and reclaimed upon deactivation.

---

### 2.7 Ephemeral FreeRTOS Tasks (`SdSpace`)

#### The Problem
The SD card storage monitor task (`SdSpace`) previously ran as a permanent FreeRTOS background task, holding a dedicated 4 KB stack plus task control block (TCB) in internal DRAM (~4.5 KB total) continuously, despite only querying FATFS once every several minutes.

#### The Architectural Solution
* Converted `SdSpace` into an **ephemeral one-shot task**:
  - Spawned on-demand when storage stats require updating.
  - Queries SD FATFS space.
  - Publishes telemetry to the central system state.
  - Cleanly self-terminates via `vTaskDelete(NULL)`, immediately returning the 4 KB stack and TCB memory to the FreeRTOS internal heap.

---

### 2.8 Network Subsystem, Stack Footprint & Rate-Limiting Guarding

1. **AsyncTCP and FreeRTOS Stack Size Boundaries:**
   - The `async_tcp` worker task stack on Core 0 must be maintained at **8192 bytes** (`CONFIG_ASYNC_TCP_STACK_SIZE=8192`). Reducing this value (e.g. to 5120 bytes) triggers silent stack starvation during incoming HTTP connection handshakes and serving large gzipped WebUI assets (~105 KB), causing browser connection timeouts (`ERR_CONNECTION_TIMED_OUT`).
   - Similarly, the primary Arduino `loopTask` on Core 1 must remain at **8192 bytes** (`CONFIG_ARDUINO_LOOP_STACK_SIZE=8192`).
2. **Wi-Fi Interface State Stability:**
   - Network mode reconfigurations (`WiFi.mode(WIFI_STA)`) and interface teardowns must be performed statically during boot initialization and never within asynchronous event callbacks (such as `ARDUINO_EVENT_WIFI_STA_GOT_IP`), which resets the underlying LwIP network interface (`netif`) and aborts bound listening sockets.
3. **HTTP 429 & Memory Admission Fallback Guarding:**
   - Network polling engines (`CryptoEngine`, `StockEngine`) must record timestamp updates (`cache.lastFetchTime = now`) upon encountering HTTP rate limits (429) or memory admission denials. Failing to update the timestamp causes continuous re-fetch loops on every 20 FPS frame (50 ms), saturating LwIP socket queues and causing DNS resolver lockups.
4. **mDNS Buffer Optimization:**
   - mDNS responder service records are retained only when enabled, avoiding permanent UDP broadcast parsing allocations.
5. **Worker Stack High-Water Mark Tuning:**
   - Systematically measured FreeRTOS stack usage across background tasks using `uxTaskGetStackHighWaterMark()`:
     * `FgtLoader` (Fighter engine asset loader): Reduced stack from 16 KB to 8 KB without risk of overflow (saving 8 KB).
     * `weather_fetch`: Adjusted to optimal bounded bounds.

---

### 2.9 Exact TLS Buffer Math & Hardening Against Allocation Failures

#### Measured Baseline
With the display at 4 bits (128×32), real TLS sessions complete reliably on classic ESP32 without PSRAM: the two mbedTLS record buffers (**16,717 B each**) allocate while the largest free block is **50 to 64 KB**.

#### Hardening Rules
* **Exact Admission Math:** `hasTlsRecordBufferHeadroom()` trusts a single block only when `largest >= 35,000 B`; otherwise, it performs a non-throwing live probe of two 16,717 B allocations.
* **Bounded Connection Timeouts:** `WiFiClientSecure::connect(host, port, timeoutMs)` enforces strict timeouts (2,500 ms) instead of the 30-second default, preventing Core 1 display stalls on unreachable hosts.
* **Low-Heap Mutation Guard (503 Service Unavailable):** Web handlers modifying configuration (`POST /api/instances`, `POST /api/rotation`) invoke `rejectIfLowHeap()`: if largest block $< 12\,\text{KB}$ or free internal DRAM $< 24\,\text{KB}$, the server responds with a body-less `503 + Retry-After: 2`, avoiding Out-Of-Memory aborts under concurrent TLS/WiFi traffic.

---

### 2.10 Teardown-Then-Measure Sandbox Rotation Sequence (S12)

#### The Problem: Decision on a Polluted Heap
Previously, `DisplayRuntime::maybeReconfigurePipelineFor` measured available DRAM while the outgoing engine and previous DMA panel were still active in memory. With an 8-bit panel and an active engine occupying ~45 KB, the measured contiguous block was depressed to $\sim 13\,\text{KB}$. Consequently, the selector falsely concluded that a 4-bit pipeline could not fit and dropped to 2-bit color depth (depth flapping $8 \to 4 \to 2 \to 4 \to 8$).

#### The Architectural Solution
ArcadeMatrix decouples **Static Compatibility Evaluation** from **Runtime Allocation**:
- **Static Compatibility (`ReferenceCapability` mode):** Evaluated against the static `ReferenceMemoryProfile` (Invariant 5), completely independent of instantaneous heap.
- **Runtime Allocation:** Executes a deterministic **teardown-then-measure** lifecycle sequence:
  1. `oldEngine->deactivate()` releases engine-owned dynamic memory.
  2. If pipeline reconfiguration is required, `releasePanel()` fully tears down the existing HUB75 DMA buffers (~18 to 36 KB).
  3. `m_surface->_backend = nullptr` is unconditionally cleared (preventing dangling pointer panics).
  4. Measure clean heap: the largest contiguous block expands cleanly to **50,000 B – 64,000 B**.
  5. Select target color depth: evaluates that 4 bits fits comfortably without flapping.
  6. Allocate target DMA bitplanes and offscreen canvas on the clean, unfragmented heap.
  7. Initialize and activate target engine.

---

### 2.11 Boot Heap Layout & Persistent System Zone Consolidation (S13)

#### The Problem: Survivor Splitting in Middle DRAM
In ESP-IDF `multi_heap`, allocations grow bottom-up. On boot, if permanent networking, system stacks, or WebServer route lambda closures are registered *after* display surface initialization, they allocate directly in the middle of Region 2 user DRAM (`0x3ffeab84..0x3fff4000`). Specifically, if the Wi-Fi driver, lwIP sockets, DHCP client, mDNS responder (with its 4,096 B task stack), SNTP client, WebServer routes, AudioHub, and `Core0LifecycleDispatcher` ("Lifecycle0", 3,072 B task stack) allocate *after* `matrixEngine.begin()`, their memory sits **above the Matrix DMA buffer**. When the display panel is later released during engine rotation (`releasePanel()`), the freed DMA memory remains **trapped as an isolated hole**, incapable of coalescing with the upper free heap. The largest contiguous block remained severely depressed (~22 KB instead of 60 KB), starving `AnimatedGIF` (which requires 24,172 B contiguous) and forcing 8-bit panels to downgrade to 4 or 2 bits.

#### The Architectural Solution
In `AppRuntime.cpp`, the entire boot sequence was re-ordered into two strictly segregated zones:
1. **Persistent System Zone (Step 3):**
   - Wi-Fi driver, STA association, DHCP negotiation, and secondary public DNS resolvers (1.1.1.1, 8.8.8.8).
   - mDNS responder initialization and its 4 KB FreeRTOS worker stack.
   - SNTP time synchronization (`configTzTime`).
   - WebServerAPI route compilation (~70 static closures) and the 8,192 B `async_tcp` worker task.
   - `AudioHub` state and mutex initialization.
   - `Core0LifecycleDispatcher` ("Lifecycle0" task, 3,072 B stack).
   - **Heap Boundary:** All permanent system structures allocate in lower DRAM (`0x3ffe0000..0x3ffee000`). Once Step 3 completes, **zero further permanent allocations** may occur.
2. **Volatile Sandbox Zone (Step 4 & Runtime):**
   - Matrix DMA bitplanes (`matrixEngine.begin()`), off-screen canvas, and active engine working buffers operate exclusively in upper DRAM (`0x3ffee000..0x3fffffff`).
   - When the panel is released during rotation (`releasePanel()`), the entire Sandbox Zone tears down cleanly from top to bottom, coalescing into an unbroken block of **50,000 B to 65,000 B**.

---

### 2.12 Two-Stage Lifecycle Reconciliation (Invariants 15 & 16)

To reconcile Core 1 real-time hot path constraints (zero blocking, zero mutex, zero allocation) with complete resource cleanup:
* **Stage 1 — Non-Blocking Deactivation (`deactivate()` on Core 1):**
  - Executes synchronously on Core 1 during `transitionSession()`.
  - Strictly state-only: clears active atomic flags (`m_isActive = false`) and triggers non-blocking transport abort (`net::SecureHttpClient::abortSessionsOwnedBy(ownerId)`).
  - Performs **zero memory allocations**, **zero task delays**, and **zero blocking loops**.
* **Stage 2 — Physical Quiescence & Destruction (`shutdownForDestruction()` on Core 0):**
  - Executes asynchronously on Core 0 via the engine retirement queue before instance destruction.
  - Signals cooperative worker exit (`DashFetch`), waits up to 300 ms for thread exit, and reclaims task stack memory (8,192 B) without Use-After-Free (UAF) risk.

---

### 2.13 Network Transaction Consolidation & Keep-Alive TLS (S15)

#### The Problem: Repetitive TLS Handshake Pressure
Fetching market data for 4 cryptos and 4 stocks individually triggered up to 8 distinct TLS handshakes. Each handshake required ~35-40 KB of transient DRAM. Multiple sequential handshakes dramatically increased memory fragmentation and the probability of colliding with incoming Wi-Fi RX bursts.

#### The Architectural Solution
* **CoinGecko Batching:** Queries `/api/v3/coins/markets?vs_currency=...&symbols=...` in **1 single HTTPS GET request**, fetching all configured cryptocurrency quotes in a single TLS handshake.
* **Yahoo Finance Keep-Alive Session:** Reuses a persistent [`net::SecureHttpSession`](../src/core/net/SecureHttpClient.h) with HTTP/1.1 keep-alive (`Connection: keep-alive`) across all requested stock tickers. All stock symbols are fetched sequentially on the **same TLS socket**, requiring **one TLS handshake per successful keep-alive batch session**.
* **Cache-Miss Consolidation:** When an engine experiences a cache miss for one symbol, it batch-refreshes all stale configured symbols in that same session. Subsequent rotations hit the RAM cache instantly with zero network delay and zero memory allocation.

---

### 2.14 3-Level Icon Cache Hierarchy & Lightweight JPEGDEC (S16)

#### The Problem: Fatal PNG Decoder Footprint
`DashboardDataProvider` historically attempted to decode 8×8 market icons using `PNGdec`. The `PNG` object embeds a 32 KB internal zlib sliding window (`sizeof(PNG) = 34,288 B`). Calling `new PNG()` on a classic ESP32 running a 4-bit panel (where the largest free block is ~18-25 KB) reliably caused `std::bad_alloc` aborts.

#### The Architectural Solution
ArcadeMatrix completely replaced `PNGdec` with a **3-Level Icon Cache Architecture** coordinated by `IconService`:
1. **L1 (RAM Cache):** In-memory decoded RGB565 bitmaps (`CachedIcon`), providing instant access without disk or network I/O.
2. **L2 (SD Card Cache):** Persistent local files under `/crypto_icons/<sym>.jpg` and `/stock_icons/<sym>.jpg`.
3. **L3 (Network Proxy via `images.weserv.nl`):**
   - Icon downloads route through `images.weserv.nl` over **plain HTTP** (zero TLS RAM overhead on the ESP32).
   - The proxy transcodes web icons to JPEG and resizes them.
   - Images are decoded using `JPEGDEC`, which requires only **~2,500 bytes of heap** (a >92% reduction compared to `PNGdec`).
* **Resilience:** If the network or proxy is unreachable, the engine serves icons from SD (L2); if absent from SD, it gracefully renders text without an icon, eliminating crashes.

---

### 2.15 Presentation-Aware Deferred Network Polling (S17)

On classic ESP32 (`!psramFound()`), background market and weather network fetches are deferred while a 4-bit panel is actively presenting once initial cache is populated, eliminating transient heap contention and display glitching without functional degradation.

---

### 2.16 Transition-Window Prefetching & Mid-Presentation TLS Hardening (S18)

#### The Problem: In-Presentation TLS Execution & Engine Survivor Leaks
When `StockEngine` or `CryptoEngine` switched to chart display mode, missing historical candles triggered on-demand HTTPS queries directly inside `update()`. Attempting a 40 KB mbedTLS handshake while HUB75 DMA was actively scanning caused allocation aborts (`MBEDTLS_ERR_MPI_ALLOC_FAILED (-16)`, `MBEDTLS_ERR_X509_ALLOC_FAILED (-10368)`). Additionally, `GifEngine::deactivate()` retained `lastPlayedGif` and `m_configuredFolders`, leaving dynamic survivor strings in the Sandbox Zone.

#### The Architectural Solution
1. **Transition-Window Prefetching (`prefetchData()` & `fetchCombined()`):**
   - In `DisplayRuntime::maybeReconfigurePipelineFor()`, before allocating the new display panel, `targetEngine->prefetchData()` executes inside the clean, DMA-released memory window (where 70 to 90 KB is free).
   - `StockEngine::prefetchData()` and `CryptoEngine::prefetchData()` invoke `fetchCombined()` to retrieve both real-time quotes and historical chart points in a single keep-alive TLS session.
   - During active presentation, `update()` renders charts strictly from local RAM cache without performing any blocking TLS calls.
2. **Strict TLS Admission Guarding:**
   - `NetworkBudget::canStartTlsSession()` enforces `largest >= TLS_MIN_COMBINED_BLOCK` (40 KB). Any mid-presentation network attempt during active scanning is cleanly denied by budget without touching mbedTLS.
3. **Zero-Survivor Deactivation Cleanups:**
   - `GifEngine::deactivate()` unconditionally clears `lastPlayedGif = String();` and invokes `std::vector<String>().swap(m_configuredFolders)`.
   - The Sandbox Zone coalesces cleanly to $\ge 50\text{--}65\text{ KB}$, enabling `AnimatedGIF` (24,172 B) and 8-bit double-buffered display modes to allocate with complete reliability.

---

## 3. Quantitative Impact & Memory Baseline Comparison

Measurements taken on **ESP32dev (Xtensa Dual-Core 240 MHz, No PSRAM)** driving a **128×32 HUB75 matrix**:

| Metric | Before Optimizations | After Consolidation | Net Headroom Gain |
| :--- | :---: | :---: | :---: |
| **Idle Free Internal DRAM** | 38,120 bytes | **56,152 bytes** | **+18,032 bytes (+47.3%)** |
| **Largest Contiguous Heap Block at Boot** | 21,840 bytes | **51,044 bytes** | **+29,204 bytes (+133.7%)** |
| **Largest Contiguous Block Post-Teardown** | 18,420 B – 27,636 B (fragmented) | **50,000 B – 64,000 B** | **Unbroken coalesced heap** |
| **DMA Allocation (8-bit vs 4-bit dynamic)** | 36,480 bytes (fixed) | **18,240 bytes (dynamic)** | **+18,240 bytes during TLS** |
| **TLS Handshakes per Dashboard Cycle** | 8 to 16 individual handshakes | **1 CoinGecko GET + 1 Yahoo session** | **Consolidated sessions** |
| **Icon Decoding Memory** | 34,288 bytes (`PNGdec`) | **~2,500 bytes (`JPEGDEC`)** | **-92.7% heap reduction** |
| **OOM Aborts / System Panics** | 5 distinct crash vectors | **0 aborts on validated soak** | **Deterministic stability** |

---

## 4. Validation Strategy & Three Levels of Proof

To ensure that empirical success translates into verifiable architectural rigor:

* **Level A — Proven by Source Code & Contracts:**
  - Removal of `PNGdec` (~35 KB) from runtime memory paths.
  - Non-blocking `deactivate()` on Core 1 vs Core 0 `shutdownForDestruction()` (Invariants 15 & 16).
  - Streaming JSON serialization in `ConfigLoader::saveToSD`.
* **Level B — Proven by Memory Instrumentation (MemTrace):**
  - Boot-time consolidation verified: all ~70 route closures consistently packed below `0x3ffee000`.
  - Teardown contiguous block verified: largest free block expanding to 50–64 KB upon engine deactivation.
* **Level C — Proven by Hardware Workload (Physical esp32dev Qualification):**
  - Validated on physical ESP32 Dev Module (128×32 HUB75, no PSRAM, active Wi-Fi).
  - Continuous multi-cycle rotation through `clock -> crypto_btc -> gif -> dashboard`.
  - Concurrent WebUI interactions (`/api/system`, `/api/instances`, `/api/rotation`).

---

## 5. Architectural Rules & Invariants Summary

* **Invariant 14 (Transition Resource Reclamation):** Outgoing engines must fully surrender transient buffers before incoming engines initialize.
* **Invariant 15 (Allocation-Free Deactivation):** `deactivate()` must never allocate heap memory; it only frees, closes, and swaps out existing containers.
* **Invariant 16 (Two-Stage Quiescent Deactivation):** `deactivate()` on Core 1 establishes immediate logical rendering quiescence; `shutdownForDestruction()` on Core 0 terminates background tasks, sockets, and I/O handles before shared resource release.
* **Invariant 19 (DMA Isolation):** HUB75 DMA framebuffers are accessed solely through `IDrawingSurface` transactions.
* **Invariant 21 (HUB75 Output Isolation):** During presentation pipeline reconfiguration, OE remains asserted (HIGH) until the first valid frame of the new pipeline is committed.
