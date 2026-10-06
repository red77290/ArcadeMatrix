# Modelo de Memoria y Arquitectura de Presentación ArcadeMatrix V4

> **Especificación Normativa**  
> **Estado:** Arquitectura Central  
> **Plataformas Objetivo:** ESP32 Estándar (`esp32dev`) y ESP32-S3 (`esp32s3_waveshare`)

---

## 1. Filosofía Arquitectónica y Dominios de Memoria

ArcadeMatrix gestiona la memoria de forma dinámica según sus atributos de hardware en ESP-IDF:

* **DRAM Interna General (`MALLOC_CAP_8BIT`):** Pilas FreeRTOS, buffers mbedTLS.
* **DRAM Interna DMA (`MALLOC_CAP_DMA`):** Bitplanes HUB75, buffers I2S.
* **PSRAM Externa (`MALLOC_CAP_SPIRAM`):** Caché de fotogramas GIF, fuentes, audio PCM.

> [!WARNING]
> **Vistas Filtradas por Capacidades:**  
> `MALLOC_CAP_DMA` es un subconjunto de la DRAM interna. **Está estrictamente prohibido sumar** `internalFreeBytes + dmaFreeBytes`.

---

## 2. Dimensionamiento DMA HUB75

La fuente de verdad es `Hub75DmaLayout::calculateBytes()`:
$$\text{Bytes DMA} = \left(\frac{\text{Altura}}{2}\right) \times \text{Profundidad} \times (\text{Anchura} \times 2) \times \text{Buffers}$$

---

## 3. Profundidad Dinámica Determinista ($8 \leftrightarrow 4$)

* **`configuredDepth`:** Ajuste del usuario (por defecto 8 bits).
* **`requestedDepth`:** Decisión de rotación según necesidades TLS.
* **`effectiveDepth`:** Profundidad real instalada.
* **`fallbackUsed`:** `effectiveDepth != requestedDepth`.

---

## 4. Transacción de Presentación e Invariante P0

La señal `OE` permanece en `HIGH` (panel apagado) hasta que:
1. Se asigne la memoria DMA.
2. Se construyan las tablas LUT.
3. Se confirme el fotograma 0 (`firstFrameCommitted == true`).
Si la asignación falla, `OE` permanece en `HIGH` (`PresentationRecovery`) para evitar cualquier artefacto óptico.
