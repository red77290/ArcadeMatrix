# Arquitectura de Optimización y Recuperación de Memoria: TLS y Renderizado de Alta Fidelidad en ESP32

## 1. Resumen Ejecutivo y Desafío Fundamental

El ESP32 clásico (doble núcleo Xtensa LX6, sin PSRAM) dispone de aproximadamente 320 KB de SRAM interna. Sin embargo, tras la inicialización del núcleo FreeRTOS, los bloques de control del sistema, las pilas de interrupción y los búferes de la capa MAC Wi-Fi, únicamente quedan disponibles entre **~60 y 80 KB de DRAM interna** para el firmware de la aplicación.

Controlar una matriz LED mediante HUB75 DMA impone una presión de memoria severa:
* En una **matriz de 128×32** (4.096 LEDs RGB), una profundidad de color de 8 bits requiere hasta **~32 KB de DRAM DMA contigua** (además de los descriptores I2S en anillo).
* Una solicitud HTTPS segura impulsada por **mbedTLS** (necesaria para APIs de Clima, Spotify, Cripto, Bolsa y Noticias) requiere entre **20 y 25 KB de DRAM interna libre** durante la negociación TLS (contexto SSL, tablas de cifrado, búferes de entrada/salida).
* Las arquitecturas de renderizado tradicionales (doble búfer DMA) requerían 32 KB adicionales, provocando inmediatamente fallos por falta de memoria (*Out-Of-Memory*) o fragmentación crítica del heap.

Mediante una serie sistemática de optimizaciones por capas, ArcadeMatrix recuperó más de **~24 KB de DRAM estática** y **~26 KB de bloque contiguo libre**, permitiendo un renderizado fluido a 60 FPS junto a operaciones TLS 100% confiables en paneles de 128×32.

---

## 2. Optimizaciones Arquitectónicas Implementadas

### 2.1 Pipeline Single DMA + Lienzo Fuera de Pantalla (`canvas_single`)

#### El Problema
Las bibliotecas estándar de HUB75 asignan dos búferes DMA completos (doble búfer de hardware) para evitar el parpadeo y desgarro visual (*tearing*), consumiendo $2 \times 32\,\text{KB} = 64\,\text{KB}$ en 128×32 a 8 bits. Esto dejaba prácticamente cero memoria contigua para tareas de red.

#### La Solución Arquitectónica
ArcadeMatrix introdujo el pipeline `canvas_single`:
1. **Búfer DMA Único:** Un único búfer físico explorado continuamente por el periférico I2S DMA.
2. **Lienzo Fuera de Pantalla:** Los motores dibujan en un lienzo de memoria SRAM intermedio (`CanvasBufferedSurface`).
3. **Presentación Transaccional:** `IDrawingSurface::present()` transfiere únicamente las filas modificadas (*dirty rows*) hacia el búfer DMA durante ventanas de escaneo seguras, eliminando el desgarro visual y ahorrando un búfer físico completo (~16 a 32 KB recuperados).
4. **Aislamiento Estricto de DMA (Invariante 19):** Los motores de renderizado nunca acceden directamente a la memoria física DMA (ver [ARCHITECTURE_ES.md](ARCHITECTURE_ES.md)).

---

### 2.2 Pipeline de Presentación Dinámico (Adaptive Color Depth $8 \leftrightarrow 4$ / $6 \leftrightarrow 4$)

#### El Problema
Las animaciones gráficas complejas (GIFs, sprites de Street Fighter, textos Marquee) requieren una profundidad de 8 o 6 bits para máxima fidelidad visual, pero su tamaño de DMA priva a mbedTLS de memoria. Por el contrario, fijar la matriz en 4 bits permanentemente degrada la calidad gráfica el 100% del tiempo.

#### La Solución Arquitectónica
En lugar de forzar un compromiso estático al iniciar el sistema, el **Dynamic Presentation Pipeline** reconfigura los recursos de hardware durante la ventana de transición entre motores:
* **Motores Gráficos (GIFs, Fighter, Marquee, Reloj):** Se muestran con la profundidad manual preferida (8 o 6 bits).
* **Motores de Red TLS (Clima, Cripto, Bolsa, Spotify, Noticias):** Conmutan temporalmente a **4 bits** (`TLS_HOT_RELOAD_COLOR_DEPTH = 4`), reduciendo el búfer DMA de ~32 KB a ~16 KB y liberando **16 KB de DRAM contigua** justo antes de la negociación mbedTLS.
* **Transacción de Presentación de Hardware (Invariante 21):**
  1. `OE = HIGH` (Output Enable activo: panel completamente negro en hardware).
  2. Parada del controlador I2S DMA.
  3. Liberación del búfer DMA anterior.
  4. Asignación del nuevo búfer DMA a la profundidad objetivo.
  5. Reinicio del controlador I2S DMA.
  6. Renderizado y commit del primer frame válido en el lienzo fuera de pantalla.
  7. `OE = LOW` (Output Enable desactivado: reanudación activa de la imagen).
* **Cero Glitch / Invisibilidad Total:** La transacción completa se ejecuta en **menos de 30 ms** (alrededor de 1.5 frames a 60 FPS), imperceptible durante la rotación entre motores.

---

### 2.3 Fidelidad de Color y Sobrescritura Dinámica de LUTs (`FastMatrixPanel::initLuts`)

#### El Problema
La biblioteca base `ESP32-HUB75-MatrixPanel-I2S-DMA` está compilada con tablas fijas de conversión de luminancia en 8 bits (`lumConvTab_8bit`). Cuando la profundidad de color se reduce a 6, 5 o 4 bits en tiempo de ejecución, el mapeo de color falla:
- Los desplazamientos de bits de los planos (*bitplanes*) se desbordan o desalinean, provocando saturación blanca, inversiones de tono y posterización severa.
- La biblioteca base no recalcula dinámicamente sus tablas de cuantificación internas tras el inicio.

#### La Solución Arquitectónica
`FastMatrixPanel` (derivada de `MatrixPanel_I2S_DMA` en `src/core/MatrixEngine.cpp`) sobrescribe completamente la conversión de color y el despacho de planos de bits:
1. **Generación Dinámica de LUTs (`FastMatrixPanel::initLuts(uint8_t depth)`):**
   ```cpp
   void FastMatrixPanel::initLuts(uint8_t depth) {
       uint8_t shift = 8 - depth;
       uint8_t round = (shift > 0) ? (1 << (shift - 1)) : 0;
       uint16_t maxVal = (1 << depth) - 1;
       // Precalcula m_lut_r[32], m_lut_g[64], m_lut_b[32] escaladas desde luminancia de 8 bits
   }
   ```
2. **Curvas Gamma y Redondeo por Canal:** Escala las curvas gamma de 8 bits a la profundidad objetivo ($2 \le \text{depth} \le 8$) con redondeo matemático exacto (`(val + round) >> shift`), evitando el aplastamiento de negros y la distorsión cromática.
3. **Recalibración Instantánea en Caliente:** Tan pronto como el pipeline de presentación se reconfigura ($8 \leftrightarrow 4$ o $6 \leftrightarrow 4$), se invoca inmediatamente `initLuts(newDepth)`, garantizando una reproducción colorimétrica 100% fiel sin requerir un reinicio del ESP32.

---

### 2.4 Quiescencia de Red Estricta y Cancelación del Lado del Cliente

#### El Problema
Si la rotación ocurre mientras una petición HTTPS está en curso, los sockets TCP abiertos y las tareas en segundo plano retienen los búferes de mbedTLS (~20-25 KB), impidiendo la reconfiguración de DMA y provocando fallos en el motor entrante.

#### La Solución Arquitectónica
* **Protocolo Formal de Desactivación:** `oldEngine->deactivate()` ejecuta un procedimiento estricto de 5 pasos:
  1. Señalización de parada a los workers mediante flags atómicos (`m_stopFetch = true`).
  2. Cancelación forzada del transporte HTTP/TLS mediante `session.abort()` (`_client.stop()`).
  3. Espera bloqueante determinista para la finalización de los hilos worker (`wait workers`).
  4. Cierre completo de identificadores de red.
  5. Liberación de cachés JSON y cotizaciones mediante el modismo `std::swap` (Invariante 15).
* **Cancelación del Lado del Cliente:** La llamada inmediata a `client.stop()` destruye el socket en LwIP. LwIP responde automáticamente con `TCP RST` a cualquier paquete entrante posterior del servidor y lo descarta sin asignar memoria DRAM.
* **Invariante N8 (Aislamiento Post-Quiescencia):** Una vez cancelada la sesión y alcanzada la quiescencia, no se permite ningún procesamiento de aplicación ni reasignación de memoria.

---

### 2.4 Asignación Perezosa de Búferes (Marquee Raw Buffer y Decodificadores GIF)

#### El Problema
`MarqueeEngine` asignaba históricamente un búfer contiguo de 8 KB (RGB565) al iniciar para soportar la visualización de iconos, incluso si el usuario sólo ejecutaba texto desplazable simple.

#### La Solución Arquitectónica
* Conversión a **asignación perezosa bajo demanda (lazy)**:
  - Inicializado en `nullptr`.
  - Asignado únicamente cuando se procesa explícitamente un archivo de imagen o icono.
  - Liberado inmediatamente mediante `freeRawBuffer()` en modos de solo texto o al desactivar.
* Las tablas de decodificación GIF en `GifEngine` se asignan exclusivamente bajo demanda durante la reproducción activa y se liberan al desactivar.

---

### 2.5 Tareas FreeRTOS Efímeras (`SdSpace`)

#### El Problema
La tarea de monitorización del espacio en tarjeta SD (`SdSpace`) se ejecutaba permanentemente en segundo plano, consumiendo una pila de 4 KB más un bloque TCB en DRAM (~4.5 KB en total), a pesar de ejecutarse sólo cada varios minutos.

#### La Solución Arquitectónica
* Conversión de `SdSpace` en una **tarea efímera de ejecución única**:
  - Creada bajo demanda cuando se requiere actualizar el almacenamiento.
  - Consulta FATFS en la tarjeta SD.
  - Publica la telemetría en el estado global del sistema.
  - Se autodestruye limpiamente mediante `vTaskDelete(NULL)`, devolviendo inmediatamente los 4 KB de pila al heap de FreeRTOS.

---

### 2.6 Ajuste del Subsistema de Red y Tamaño de Pilas

1. **Desmantelamiento de SoftAP / Portal Cautivo:**
   - Tan pronto como se establece la conexión Wi-Fi (`WL_CONNECTED`), la interfaz SoftAP se desconecta totalmente mediante `WiFi.softAPdisconnect(true)`, liberando los búferes del controlador Wi-Fi.
2. **Optimización de Búferes mDNS:**
   - Los registros mDNS se conservan únicamente cuando el servicio local está activado.
3. **Calibración de Pilas FreeRTOS:**
   - Medición sistemática del consumo real de pilas mediante `uxTaskGetStackHighWaterMark()`:
     * `FgtLoader` (carga de sprites de Fighter): Reducción de 16 KB a 8 KB de forma totalmente segura (8 KB de DRAM recuperados).
     * `weather_fetch` / `DashFetch`: Ajustadas a límites seguros estrictos.

---

## 3. Impacto Cuantitativo y Comparativa de Memoria

Mediciones realizadas en **ESP32dev (Xtensa Dual-Core 240 MHz, Sin PSRAM)** controlando una **matriz HUB75 128×32**:

| Métrica | Antes de Optimizaciones | Después de Optimizaciones | Ganancia Neta |
| :--- | :---: | :---: | :---: |
| **DRAM Interna Libre en Reposo** | 38.120 bytes | **62.480 bytes** | **+24.360 bytes (+63.9%)** |
| **Mayor Bloque Contiguo Libre** | 21.840 bytes | **48.650 bytes** | **+26.810 bytes (+122.7%)** |
| **Asignación DMA (8 bits vs 4 bits dinámico)** | 32.768 bytes (fijo) | **16.384 bytes (dinámico)** | **+16.384 bytes en fase TLS** |
| **Memoria de Tareas Permanentes** | ~22.5 KB | **~10.0 KB** | **+12.5 KB liberados** |
| **Tasa de Éxito en Handshake mbedTLS** | ~35% (OOM frecuentes) | **100% (cero fallos de asignación)** | **Estabilidad total** |

---

## 4. Resumen de Invariantes y Reglas Arquitectónicas

* **Invariante 14 (Recuperación de Recursos en Transición):** El motor saliente debe liberar completamente sus búferes transitorios antes de que el motor entrante se inicialice.
* **Invariante 15 (Desactivación Libre de Asignaciones):** `deactivate()` nunca debe realizar asignaciones dinámicas en el heap; únicamente libera, cierra y aplica el modismo swap.
* **Invariante 16 (Quiescencia Completa de Desactivación):** `deactivate()` no retorna hasta que todas las tareas, temporizadores y flujos de I/O hayan concluido.
* **Invariante 19 (Aislamiento de Hardware DMA):** Los búferes DMA de HUB75 se acceden exclusivamente a través de `IDrawingSurface`.
* **Invariante 21 (Aislamiento de Salida HUB75):** Durante la reconfiguración del pipeline de presentación, la señal OE se mantiene inactiva (HIGH) hasta que el primer frame válido ha sido confirmado con éxito.
