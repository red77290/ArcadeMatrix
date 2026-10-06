# ArcadeMatrix v4 — Formal Invariants & Presentation Architecture

## 1. Overview
The ArcadeMatrix v4 graphical architecture strictly decouples rendering, intermediate canvas storage, bitplane encoding, and hardware DMA presentation into an invariant unidirectional pipeline:

```
Application Engine (Tetris, Clock, GIF, Visualizer, etc.)
  │
  ▼ [drawPixel / fillRect / blit565]
IDrawingSurface (CanvasBufferedSurface / DirectDmaSurface)
  │ Logical -> Physical coordinates mapping
  │ Linear RGB565 Intermediate Canvas (SRAM or PSRAM)
  ▼ [present()]
IPresentationBackend (Hub75PresentationBackend / MockPresentationBackend)
  │ Timing budget enforcement (maxBlankUs, maxFrameUs, allowBlanking)
  │ Safe window synchronization (IPresentationSynchronizer)
  ▼ [acquireDmaTarget()]
Hub75DmaTarget (DMA buffers, layout, stride, BCM depth, color LUTs)
  │
  ▼ [Hub75BulkEncoder::encode()]
HUB75 Packed Bitplanes
  │
  ▼ [commit()]
Physical HUB75 DMA Driver (MatrixPanel_I2S_DMA)
```

---

## 2. Canonical Architectural Invariants

### Invariant 1: Engine Isolation
An application engine (`IEngine`) may ONLY interact with:
- `IDrawingSurface&` (Adafruit_GFX compatible graphical SPI)
- `AppEngineContext*` / `EngineContext*`
- Private engine-specific data structures and assets.

**STRICTLY FORBIDDEN IN ENGINES:**
- `MatrixEngine*`
- `MatrixPanel_I2S_DMA*`
- `FastMatrixPanel*`
- Direct access to DMA buffers or descriptors
- Hardware HUB75 registers, OE control, or driver chips
- Direct calls to `Hub75BulkEncoder`

### Invariant 2: Canvas Ownership
`IDrawingSurface` exclusively owns the intermediate 16-bit RGB565 linear canvas (allocated either in internal SRAM or external PSRAM). It NEVER owns, manages, or allocates hardware DMA framebuffers.

### Invariant 3: Single Canonical Encoder
There is exactly ONE bitplane converter in the entire codebase: `Hub75BulkEncoder`.
All RGB565 to BCM translation passes through `Hub75BulkEncoder::encode()`. No duplicate bitplane modulation, color wrapping, or LUT transformations may exist in engines, surfaces, or matrix drivers.

### Invariant 4: Hardware Presentation Target Contract
All access to destination DMA bitplane memory is strictly encapsulated behind `Hub75DmaTarget`. The encoder receives all pitch, stride, row accessor, and color LUT information exclusively from `Hub75DmaTarget`.

### Invariant 5: Presentation Owner Contract
All hardware commits and buffer flips MUST pass through `IPresentationBackend::commit()`. `CanvasBufferedSurface` has zero knowledge of `MatrixEngine` or physical panels.

### Invariant 6: Core 1 Hot-Path Zero-Allocation & Zero-Contention
The Core 1 execution loop (`update() -> evaluate() -> transitionSession() -> render() -> present()`) must maintain zero allocations:
- Zero `malloc`, `calloc`, `realloc`, `free`, `new`, `delete`.
- Zero dynamic container resizes (`std::vector::push_back`, `String` concatenation).
- Zero `std::mutex` acquisition on Core 1 (Core 0 uses dedicated `producerMutex`).
- Zero blocking network, SD card, or filesystem I/O.

### Invariant 7: Centralized Memory & Pipeline Authority
A single authority (`PipelineSelectionPolicy` via `DisplaySurfaceFactory`) determines:
- Active rendering strategy (`CANVAS_BURST_SINGLE`, `CANVAS_BURST_DOUBLE`, `DIRECT_DMA_SINGLE`, `DIRECT_DMA_DOUBLE`).
- Canvas storage tier (`SRAM`, `PSRAM`, or `NONE`).
- DMA buffer allocation mode (single vs double buffering).

`MatrixEngine` applies the pipeline configuration provided to it; it never opportunistically changes buffering modes on its own.

---

## 3. The Four Standard Rendering Pipelines

| Pipeline | Canvas Storage | DMA Buffers | Tearing Risk | RAM Footprint | PSRAM Usable | Target Platform |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`CANVAS_BURST_SINGLE`** | SRAM / PSRAM | 1 (Single) | Mitigated by Safe-Window / OE Blanking | **Lowest DMA RAM** (Halved) | Yes | ESP32 Classic / ESP32-S3 Low RAM |
| **`CANVAS_BURST_DOUBLE`** | PSRAM / SRAM | 2 (Double) | **Zero (Tear-Free)** | Standard DMA RAM | Yes | ESP32-S3 (Waveshare N32R16) |
| **`DIRECT_DMA_DOUBLE`** | NONE | 2 (Double) | **Zero (Tear-Free)** | Standard DMA RAM | No (DMA in Internal RAM) | Legacy direct rendering |
| **`DIRECT_DMA_SINGLE`** | NONE | 1 (Single) | Unmitigated | **Minimal RAM** | No | Ultra-low RAM fallback |

---

## 4. Presentation Policy Execution Sequence & Ordering Contracts

To eliminate screen tearing while respecting hardware constraints, ArcadeMatrix strictly distinguishes between single-buffered and double-buffered presentation flows:

### A. Single-Buffer Flow (`CANVAS_BURST_SINGLE`)
In single-buffer mode, bitplane encoding writes directly into the framebuffer actively scanned by I2S DMA. To prevent tearing artifacts and visual corruption during encoding, the critical section must encompass the encode phase:

```
CANVAS_BURST_SINGLE:
  1. Compute dynamic transfer estimate via PresentationTimingModel::estimateTransferUs()
  2. Wait for Safe Window: synchronizer->waitForSafeWindow(estimateUs, maxBlankUs)
  3. Blank Display (Output Enable off / brightness = 0)
  4. Encode RGB565 Canvas to actively scanned DMA buffer via Hub75BulkEncoder
  5. Cache write-back (if PSRAM)
  6. Commit hardware refresh
  7. Unblank Display (restore brightness)
  8. Budget validation: blankUs <= maxBlankUs, totalPresentUs <= maxFrameUs
```

### B. Double-Buffer Flow (`CANVAS_BURST_DOUBLE`)
In double-buffer mode, encoding occurs off-screen into the non-scanned back buffer. The critical safe window section is acquired at the very last moment solely for the instantaneous descriptor pointer swap:

```
CANVAS_BURST_DOUBLE:
  1. Encode RGB565 Canvas into inactive back buffer via Hub75BulkEncoder
  2. Cache write-back (if PSRAM)
  3. Compute swap transfer estimate (25 µs fixed descriptor flip)
  4. Wait for Safe Window: synchronizer->waitForSafeWindow(25, maxBlankUs)
  5. Optional transient blanking (if policy.allowBlanking == true)
  6. Hardware DMA descriptor swap (flipDMABuffer())
  7. Optional unblanking
  8. Budget validation: totalPresentUs <= maxFrameUs
```

---

## 5. Timing Models & Canonical DMA Layout

### Physics-Based PresentationTimingModel
The presentation pipeline derives realistic safe-window timeouts and transfer deadlines dynamically via `PresentationTimingModel`:
- **Bitplane Encoding Rate**: Evaluated on 240 MHz Xtensa CPU cores (~20 ns per pixel-plane operation).
- **Cache Writeback Overhead**: Evaluated at ~40 MB/s for external SPI/OPI PSRAM flushes.
- **Hardware Swap Latency**: Fixed at $25\,\mu\text{s}$ for DMA descriptor address flips.

### DmaMemoryLayout Scan Topology
`DmaMemoryLayout` defines the canonical memory layout for the HUB75 scan topology currently supported by ArcadeMatrix:
- Standard 1/16 and 1/32 dual-row parallel scan panels ($rows = \frac{height}{2}$).
- $bytesPerBuffer = \text{rows} \times \text{colorDepth} \times (\text{width} \times \text{sizeof(uint16\_t)})$.
- Total DMA allocation is calculated as $\text{bufferCount} \times bytesPerBuffer$.

---

## 6. Presentation Outcome Codes
- `PresentationResult::Ok` (0): Frame presented successfully within all budgets.
- `PresentationResult::BackendUnavailable` (1): Surface has no attached presentation backend.
- `PresentationResult::DmaTargetUnavailable` (2): Target DMA memory descriptor was null or unallocated.
- `PresentationResult::SafeWindowTimeout` (3): Synchronizer did not grant safe window before deadline.
- `PresentationResult::BlankBudgetExceeded` (4): Output Enable blanking exceeded `maxBlankUs`.
- `PresentationResult::FrameBudgetExceeded` (5): Total presentation latency exceeded `maxFrameUs` budget.
- `PresentationResult::InvalidTarget` (6): Target dimensions or rows per frame invalid.
- `PresentationResult::EncodingError` (7): Canvas buffer was null or corrupted.

