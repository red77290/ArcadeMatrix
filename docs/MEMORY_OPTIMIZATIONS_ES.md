# Arquitectura de Optimización y Recuperación de Memoria: TLS y Renderizado de Alta Fidelidad en ESP32

## 1. Resumen Ejecutivo y Desafío Fundamental

El ESP32 clásico (doble núcleo Xtensa LX6, sin PSRAM) dispone de aproximadamente 320 KB de SRAM interna compartida entre instrucciones (IRAM), datos estáticos (.data / .bss) y heap general (DRAM). Al inicio de la función `setup()`, `heap_caps_get_free_size(MALLOC_CAP_8BIT)` reporta **~190,5 KB de DRAM libre inicial**, que disminuye rápidamente a **~56 a 62 KB** una vez inicializados los búferes Wi-Fi MAC, sockets lwIP y el servidor Web.

El control de una matriz LED mediante HUB75 DMA impone una presión de memoria extrema:
* En una **matriz de 128×32** (4.096 LEDs RGB), una profundidad de color de 8 bits requiere hasta **~36,5 KB de DRAM contigua para DMA** (más los descriptores I2S en anillo).
* Una petición HTTPS segura impulsada por **mbedTLS** (necesaria para APIs de tiempo, Spotify, criptomonedas, bolsa y noticias RSS) requiere **~35 a 40 KB de DRAM interna libre** durante la negociación TLS (contexto SSL, tablas de cifrado y dos búferes de registro de entrada/salida de 16.717 bytes).
* Las arquitecturas de renderizado tradicionales (doble búfer DMA) exigían 32 KB adicionales, provocando inmediatamente fallos por agotamiento de memoria (*Out-Of-Memory*) o fragmentación crítica del heap.

A través de un plan de optimizaciones escalonadas, ArcadeMatrix recuperó más de **~24 KB de DRAM estática** y **~30 KB de bloque contiguo libre**, permitiendo un renderizado a 60 FPS junto con operaciones TLS deterministas en paneles de 128×32 sin PSRAM.

---

## 2. Optimizaciones Arquitectónicas Realizadas

### 2.1 Pipeline Single DMA + Canvas Fuera de Pantalla (`canvas_single`)

#### El Problema
Las bibliotecas HUB75 estándar asignan dos búferes DMA completos (doble búfer de hardware) para evitar el parpadeo y rasgado visual (*tearing*), consumiendo $2 \times 32\,\text{KB} = 64\,\text{KB}$ en 128×32 a 8 bits. Esto dejaba prácticamente cero DRAM contigua para las tareas de red.

#### La Solución Arquitectónica
ArcadeMatrix introdujo el pipeline `canvas_single`:
1. **Búfer DMA Único:** Un único búfer físico escaneado continuamente por el periférico I2S DMA.
2. **Canvas Fuera de Pantalla:** Los motores dibujan en un canvas de memoria SRAM intermedia (`CanvasBufferedSurface`).
3. **Presentación Transaccional:** `IDrawingSurface::present()` transfiere únicamente las filas modificadas (*dirty rows*) al búfer DMA durante intervalos de refresco seguros, eliminando el rasgado y ahorrando un búfer completo (~16 a 32 KB recuperados).
4. **Aislamiento de Hardware DMA (Invariante 19):** Los motores de renderizado nunca acceden directamente a la memoria física DMA (véase [ARCHITECTURE_ES.md](ARCHITECTURE_ES.md#222-invariantes-arquitectónicos-formales-15-a-20)).

---

### 2.2 Pipeline de Presentación Dinámica y Optimizador Automático de Color ($8 \leftrightarrow 7 \dots 2$ Profundidad Adaptativa)

#### El Problema
Los gráficos de alto contraste (GIFs, sprites de combate arcade, marquesinas, esferas de reloj) se benefician notablemente de 8 bits de color (16,7 millones de colores), pero su huella DMA agota la memoria de mbedTLS en hardware sin PSRAM. Por otro lado, limitar permanentemente a 4 bits degrada la estética visual de forma continua, mientras que un límite estático a 6 bits restringe innecesariamente motores gráficos capaces. Además, un aumento de profundidad ingenuo en caliente corre el riesgo de inducir un fallo inmediato por falta de memoria si la DRAM se fragmentó durante tareas de red.

#### La Solución Arquitectónica
El **Dynamic Presentation Pipeline** combina la quiescencia de hardware con un modelo predictivo riguroso en `PipelineSelectionPolicy::resolveTargetDepth`:

1. **Evaluación Multidimensional de Capacidad Matemática:**
   Entre turnos de rotación (estrictamente después de que `oldEngine->deactivate()` alcanza la quiescencia y antes de que `newEngine->activate()` asigne memoria), `DisplayRuntime` consulta la DRAM y memoria DMA disponibles. Las profundidades candidatas $D \in [8 \dots 2]$ se evalúan en orden descendente contra cuatro límites matemáticos:
   * **Margen de DRAM Interna Libre:**
     $$\widehat{F}(D) = \text{currentFreeInternalHeap} + (\text{currentDepth} - D) \times \text{bytesPerBit} \ge \text{SYSTEM\_MIN\_HEADROOM\_RESERVE} + \text{req.internalPersistentBytes} + \text{netReserve} + \text{audioReserve}$$
   * **Mayor Bloque de DRAM Contiguo:**
     $$\widehat{L}(D) = \text{currentLargestBlock} + (\text{currentDepth} - D) \times \text{bytesPerBit} \ge \text{minContiguousNeeded}$$
     donde $\text{minContiguousNeeded} = \text{NetworkBudget::TLS\_MIN\_COMBINED\_BLOCK}$ ($28.672\,\text{bytes}$) para motores TLS.
   * **Capacidad DMA Interna:**
     $$\widehat{Dma}(D) = \text{currentFreeDma} + (\text{currentDepth} - D) \times \text{bytesPerBit} \ge \text{minDmaNeeded}$$
     donde $\text{minDmaNeeded} = \text{NetworkBudget::TLS\_MIN\_FREE\_DMA}$ ($16.384\,\text{bytes}$) para la aceleración hardware SHA de esp-sha.
   * **Factibilidad en PSRAM:** Verificada para placas con PSRAM (como ESP32-S3), asegurando un modelo matemático coherente en todas las plataformas.

2. **Ajuste Dinámico Continuo ($8 \leftrightarrow 7 \leftrightarrow 6 \leftrightarrow 5 \leftrightarrow 4 \leftrightarrow 3 \leftrightarrow 2$):**
   * **Motores de Red TLS (Crypto, Bolsa, Spotify, GNews, Tiempo):** Reducen la profundidad de forma matemática al valor seguro más alto (típicamente 4 bits, o 2 bits bajo presión extrema), liberando hasta **16 a 24 KB de DRAM contigua** justo antes de la negociación mbedTLS.
   * **Motores Gráficos (GIFs, Fighter, Reloj, Canvas, Matrix, Marquee):** Evaluados desde 8 bits hacia abajo, restaurando los **8 bits completos** siempre que sea seguro. En paneles 128×32 y 64×32 sin PSRAM, los relojes y motores gráficos funcionan en 8 bits nativos.
   * **Integración WebUI:** Al activar "Dynamic Presentation Pipeline", el selector manual de profundidad de color se deshabilita automáticamente, mostrando una nota explicativa sobre el control automático.

3. **Protección de Límite Mínimo (2 Bits):**
   `FastMatrixPanel::initLuts` soporta hasta 2 bits de color ($2 \le \text{depth} \le 8$), garantizando un modo degradado funcional bajo fragmentación extrema sin cortes de imagen.

4. **Transacción de Presentación en Hardware (Invariante 21):**
   Durante la transición entre motores, la transacción se ejecuta ordenadamente:
   1. `oldEngine->deactivate()` establece la quiescencia lógica en Core 1.
   2. `OE = HIGH` (Output Enable activo: pantalla apagada en hardware).
   3. Desmontaje del pipeline DMA activo.
   4. Intento de asignación DMA objetivo.
   5. En caso de fallo, intento progresivo de repliegue ($4 \to 2$ bits).
   6. Regeneración de tablas LUT mediante `FastMatrixPanel::initLuts(effectiveDepth)`.
   7. Confirmación del Frame 0 (trama negra determinista) vía `m_presentationBackend->commitFirstFrame()`.
   8. **Invariante P0:** `OE = LOW` (Output Enable liberado) estrictamente tras `firstFrameCommitted == true`.

---

### 2.3 Fidelidad de Color y LUTs Dinámicas (`FastMatrixPanel::initLuts`)

`FastMatrixPanel` reemplaza completamente la conversión de color y el despacho de bitplanes:
1. **Generación Dinámica de Tablas (`FastMatrixPanel::initLuts(uint8_t depth)`):**
   Recalcula las tablas `m_lut_r`, `m_lut_g`, `m_lut_b` a la profundidad objetivo.
2. **Corrección Gamma por Canal:** Aplica redondeo matemático adecuado (`(val + round) >> shift`), evitando el aplastamiento de tonos oscuros.
3. **Recalibración Inmediata:** Se ejecuta en cada cambio de profundidad ($8 \leftrightarrow 4$ o $6 \leftrightarrow 4$), asegurando fidelidad cromática sin reinicios.

---

### 2.4 Quiescencia de Red y Aborto de Sockets por Ámbito

* **Protocolo Formal de Desactivación:** `oldEngine->deactivate()` ejecuta la secuencia:
  1. Señaliza la detención de workers mediante flags atómicos (`m_stopFetch = true`).
  2. Aborta de inmediato el transporte HTTP/TLS mediante aborto específico (`net::SecureHttpClient::abortSessionsOwnedBy(ownerId)`).
  3. Espera de forma determinista la finalización de los workers durante la destrucción en Core 0 (`shutdownForDestruction()`).
  4. Cierra descriptores de transporte y purga cachés usando el modismo de swap (`std::vector<T>().swap(vec)`).
* **Aborto en Cliente:** El cierre inmediato destruye el descriptor en LwIP, que responde con TCP RST a cualquier paquete entrante posterior sin consumir DRAM.

---

### 2.5 Streaming HTTP Sin Asignación Dinámica

* **Asignación Fija en Pila:** Se sustituyeron las reservas dinámicas en el heap por matrices de capacidad fija en la pila para el parseo de respuestas REST:
  ```cpp
  constexpr size_t MAX_RAW_PRICES = 320;
  float rawPrices[MAX_RAW_PRICES];
  ```
* **Cero Contención con mbedTLS:** Los flujos se procesan directamente en la pila sin tocar el heap, evitando fallos por `std::bad_alloc`.

---

### 2.6 Asignación Perezosa de Búferes (Marquee y GIFs)

* El búfer RGB565 de `MarqueeEngine` (8 KB) se asigna **únicamente bajo demanda** cuando se cargan iconos, liberándose en modo de solo texto.
* Las tablas de decodificación de `GifEngine` se asignan exclusivamente durante la reproducción activa.

---

### 2.7 Tareas FreeRTOS Efímeras (`SdSpace`)

La tarea de monitorización de espacio en tarjeta SD (`SdSpace`) se transformó en una **tarea efímera de una sola ejecución**: se crea bajo demanda, consulta FATFS, publica los datos y se autodestruye con `vTaskDelete(NULL)`, liberando sus 4 KB de pila.

---

### 2.8 Subsistema de Red y Calibración de Pilas

1. **Pilas AsyncTCP y loopTask:** Mantenidas obligatoriamente en **8192 bytes** para evitar desbordamientos durante transferencias WebUI de 105 KB comprimidos.
2. **Estabilidad Wi-Fi:** `WiFi.mode(WIFI_STA)` se ejecuta de forma estática en el arranque para evitar reinicializaciones de la interfaz `netif` de LwIP en caliente.
3. **Protección HTTP 429:** Los motores actualizan la marca de tiempo del último intento (`cache.lastFetchTime = now`) incluso tras errores 429 o falta de memoria, previniendo bucles de reintento a 20 FPS (50 ms).

---

### 2.9 Cálculo Exacto de Búferes TLS y Endurecimiento

#### Medición
Con la pantalla a 4 bits (128×32), las sesiones TLS se completan de forma determinista en ESP32 sin PSRAM: los dos búferes de registro de mbedTLS (**16.717 bytes cada uno**) se asignan cuando el mayor bloque contiguo alcanza **50 a 64 KB**.

#### Reglas de Endurecimiento
* **Admisión Rigurosa:** `hasTlsRecordBufferHeadroom()` confía en un único bloque solo con `largest >= 35.000 bytes`; de lo contrario, sondea dos asignaciones reales de 16.717 bytes.
* **Tiempos Límite Acotados:** `WiFiClientSecure::connect(host, port, timeoutMs)` impone 2.500 ms de límite en lugar de 30 segundos, evitando bloqueos en Core 1.
* **Guarda de Bajo Heap (503 Service Unavailable):** Las rutas de modificación de configuración rechazan peticiones si el bloque mayor es $< 12\,\text{KB}$ o la DRAM libre es $< 24\,\text{KB}$, respondiendo `503 + Retry-After: 2` sin asignar memoria.

---

### 2.10 Modelo Sandbox Teardown-Then-Measure (S12)

#### El Problema: Decisión en un Heap Contaminado
Anteriormente, `DisplayRuntime::maybeReconfigurePipelineFor` evaluaba la DRAM mientras el motor saliente y el panel DMA anterior seguían activos en memoria. Con un panel de 8 bits activo, el bloque contiguo caía a $\sim 13\,\text{KB}$, provocando que el selector redujera erróneamente a 2 bits (oscilación $8 \to 4 \to 2 \to 4 \to 8$).

#### La Solución Arquitectónica
ArcadeMatrix separa de forma estricta la **Evaluación de Compatibilidad Estática** de la **Asignación en Ejecución**:
- **Compatibilidad Estática (`ReferenceCapability`):** Evaluada contra el perfil de referencia (Invariante 5), independiente del estado instantáneo del heap.
- **Asignación en Ejecución:** Aplica la secuencia determinista **desmontaje y posterior medición**:
  1. `oldEngine->deactivate()` libera el estado dinámico del motor.
  2. Si se requiere reconfigurar el pipeline, `releasePanel()` destruye los búferes DMA de HUB75 (~18 a 36 KB).
  3. Limpieza incondicional de `m_surface->_backend = nullptr` (evita punteros huérfanos).
  4. Medición del heap limpio: el mayor bloque contiguo asciende a **50.000 – 64.000 bytes**.
  5. Selección de profundidad: 4 bits se asigna con amplio margen sin oscilaciones.
  6. Asignación del nuevo pipeline DMA y canvas sobre el heap limpio y unificado.
  7. Inicialización y activación del nuevo motor.

---

### 2.11 Disposición de Heap en Arranque y Consolidación de Zona de Sistema Persistente (S13)

#### El Problema: Fragmentación por Objetos "Supervivientes" en DRAM Media
En `multi_heap` de ESP-IDF, las asignaciones crecen hacia arriba. Si la pila de red permanente, tareas del sistema o clausuras lambda de rutas del servidor Web se asignaban *después* de inicializar la pantalla, se alojaban en medio de la DRAM de usuario de la Región 2 (`0x3ffeab84..0x3fff4000`). Específicamente, si el controlador Wi-Fi, sockets lwIP, cliente DHCP, el respondedor mDNS (con su tarea de 4.096 B), cliente SNTP, rutas WebServer, AudioHub y `Core0LifecycleDispatcher` ("Lifecycle0", pila de 3.072 B) se inicializaban *después* de `matrixEngine.begin()`, su memoria quedaba situada **por encima del búfer DMA de Matrix**. Al liberar posteriormente el panel durante la rotación de motores (`releasePanel()`), la memoria DMA liberada permanecía **atrapada como un hueco aislado**, incapaz de fusionarse con el heap libre superior. El bloque contiguo máximo quedaba severamente deprimido (~22 KB en lugar de 60 KB), privando a `AnimatedGIF` (que requiere 24.172 B contiguos) y forzando a los paneles de 8 bits a degradarse a 4 o 2 bits.

#### La Solución Arquitectónica
En `AppRuntime.cpp`, toda la secuencia de arranque se reordenó en dos zonas estrictamente segregadas:
1. **Zona de Sistema Persistente (Paso 3):**
   - Controlador Wi-Fi, asociación STA, negociación DHCP y servidores DNS públicos secundarios (1.1.1.1, 8.8.8.8).
   - Inicialización del respondedor mDNS y su pila de 4 KB FreeRTOS.
   - Sincronización de hora SNTP (`configTzTime`).
   - Compilación de rutas de WebServerAPI (~70 clausuras estáticas) y tarea worker `async_tcp` de 8.192 B.
   - Estado y mutex de `AudioHub`.
   - `Core0LifecycleDispatcher` (tarea "Lifecycle0", pila de 3.072 B).
   - **Límite de Heap:** Todas las estructuras permanentes del sistema se asignan en la zona baja de la DRAM (`0x3ffe0000..0x3ffee000`). Una vez finalizado el Paso 3, se prohíbe **cualquier asignación permanente adicional**.
2. **Zona de Sandbox Volátil (Paso 4 y Ejecución):**
   - Planos de bits DMA de Matrix (`matrixEngine.begin()`), canvas fuera de pantalla y búferes de trabajo de motores operan exclusivamente en la DRAM superior (`0x3ffee000..0x3fffffff`).
   - Al liberar el panel durante la rotación (`releasePanel()`), toda la Zona de Sandbox se desmonta limpiamente de arriba a abajo, fusionándose en un bloque ininterrumpido de **50.000 B a 65.000 B**.

---

### 2.12 Conciliación de Ciclo de Vida en Dos Etapas (Invariantes 15 y 16)

Para conciliar los requisitos de tiempo real en Core 1 (sin bloqueos, sin mutex, sin asignaciones) con la liberación completa de recursos:
* **Etapa 1 — Desactivación No Bloqueante (`deactivate()` en Core 1):**
  - Ejecutada de forma síncrona en Core 1 durante `transitionSession()`.
  - Estrictamente de estado: establece `m_isActive = false` y dispara el aborto inmediato de sockets (`net::SecureHttpClient::abortSessionsOwnedBy(ownerId)`).
  - Cero asignaciones, cero demoras de tareas y cero bloqueos en Core 1.
* **Etapa 2 — Quiescencia Física y Destrucción (`shutdownForDestruction()` en Core 0):**
  - Ejecutada de forma asíncrona en Core 0 mediante la cola de retiro de motores antes de su destrucción.
  - Notifica la salida cooperativa de workers (`DashFetch`), espera hasta 300 ms su finalización y recupera la pila de 8.192 bytes de forma segura sin riesgo de Use-After-Free (UAF).

---

### 2.13 Consolidación de Transacciones de Red y Keep-Alive TLS (S15)

#### El Problema: Presión Recurrente por Múltiples Handshakes TLS
Consultar 4 criptomonedas y 4 acciones individualmente implicaba hasta 8 handshakes TLS consecutivos. Cada handshake requería ~35 a 40 KB de DRAM transitoria, aumentando el riesgo de fragmentación y colisión con ráfagas Wi-Fi RX entrantes.

#### La Solución Arquitectónica
* **Lote en CoinGecko:** Consulta `/api/v3/coins/markets?vs_currency=...&symbols=...` en **1 única petición HTTPS GET**, obteniendo todas las criptomonedas en un solo handshake.
* **Sesión Keep-Alive en Yahoo Finance:** Reutiliza una sesión persistente [`net::SecureHttpSession`](../src/core/net/SecureHttpClient.h) con HTTP/1.1 keep-alive (`Connection: keep-alive`) para todos los tickers bursátiles. Todas las cotizaciones se reciben por la **misma socket TLS**, necesitando **un solo handshake TLS por sesión de lote exitosa**.
* **Consolidación en Fallos de Caché:** Cuando un motor detecta un fallo de caché en un símbolo, actualiza en lote todos los símbolos configurados en la misma sesión. Las siguientes rotaciones leen directamente de la caché en RAM sin demoras ni peticiones de red.

---

### 2.14 Jerarquía de Caché de Iconos en 3 Niveles y JPEGDEC Ligero (S16)

#### El Problema: Huella Crítica del Decodificador PNG
`DashboardDataProvider` intentaba históricamente decodificar iconos 8×8 mediante `PNGdec`. El objeto `PNG` integra una ventana deslizante zlib de 32 KB (`sizeof(PNG) = 34.288 bytes`). Invocar `new PNG()` en un ESP32 clásico con panel a 4 bits causaba inevitablemente abortos por `std::bad_alloc`.

#### La Solución Arquitectónica
ArcadeMatrix sustituyó `PNGdec` por una **Arquitectura de Caché de Iconos en 3 Niveles** gestionada por `IconService`:
1. **L1 (Caché en RAM):** Mapas de bits RGB565 decodificados en memoria (`CachedIcon`), con acceso instantáneo sin E/S.
2. **L2 (Caché en Tarjeta SD):** Archivos locales persistentes en `/crypto_icons/<sym>.jpg` y `/stock_icons/<sym>.jpg`.
3. **L3 (Proxy de Red vía `images.weserv.nl`):**
   - La descarga se enruta mediante `images.weserv.nl` en **HTTP plano** (sin consumo de RAM TLS en el ESP32).
   - El proxy transcodifica los iconos web a JPEG y ajusta sus dimensiones.
   - Las imágenes se decodifican mediante `JPEGDEC`, que requiere únicamente **~2.500 bytes de heap** (más del 92% de ahorro respecto a `PNGdec`).
* **Resiliencia:** Si la red o el proxy fallan, la caché en SD (L2) suministra los iconos; si no existen en la SD, el motor degrada elegantemente mostrando texto sin icono (cero caídas).

---

### 2.15 Peticiones de Red Diferidas Durante Presentación Activa (S17)

En ESP32 clásico (`!psramFound()`), las peticiones periódicas de tiempo y bolsa se posponen mientras el panel opera activamente a 4 bits una vez cargada la caché inicial, eliminando contenciones de memoria y asegurando fluidez visual sin pérdidas funcionales.

---

### 2.16 Precarga en Ventana de Transición y Blindaje TLS Durante Presentación (S18)

#### El Problema: Ejecución TLS en Presentación y Supervivientes en Motores
Cuando `StockEngine` o `CryptoEngine` conmutaban a modo gráfico de velas, las velas históricas faltantes provocaban peticiones HTTPS bajo demanda directamente dentro de `update()`. Ejecutar un handshake mbedTLS de 40 KB con el DMA de HUB75 escaneando activamente causaba abortos de asignación (`MBEDTLS_ERR_MPI_ALLOC_FAILED (-16)`, `MBEDTLS_ERR_X509_ALLOC_FAILED (-10368)`). Asimismo, `GifEngine::deactivate()` conservaba `lastPlayedGif` y `m_configuredFolders`, dejando cadenas dinámicas supervivientes en la Zona de Sandbox.

#### La Solución Arquitectónica
1. **Precarga en Ventana de Transición (`prefetchData()` y `fetchCombined()`):**
   - En `DisplayRuntime::maybeReconfigurePipelineFor()`, antes de asignar el nuevo panel de visualización, se ejecuta `targetEngine->prefetchData()` dentro de la ventana de memoria limpia con DMA liberado (donde hay 70 a 90 KB libres).
   - `StockEngine::prefetchData()` y `CryptoEngine::prefetchData()` invocan `fetchCombined()` para obtener tanto cotizaciones en tiempo real como puntos de gráfico histórico en una única sesión TLS keep-alive.
   - Durante la presentación activa, `update()` renderiza los gráficos estrictamente desde la caché en RAM local sin realizar ninguna llamada TLS bloqueante.
2. **Control Estricto de Admisión TLS:**
   - `NetworkBudget::canStartTlsSession()` exige `largest >= TLS_MIN_COMBINED_BLOCK` (40 KB). Cualquier intento de red durante el escaneo activo es denegado limpiamente por presupuesto sin tocar mbedTLS.
3. **Desactivación Libre de Cero Supervivientes:**
   - `GifEngine::deactivate()` limpia incondicionalmente `lastPlayedGif = String();` e invoca `std::vector<String>().swap(m_configuredFolders)`.
   - La Zona de Sandbox se fusiona limpiamente hasta $\ge 50\text{--}65\text{ KB}$, permitiendo que `AnimatedGIF` (24.172 B) y los modos de doble búfer de 8 bits se asignen con absoluta fiabilidad.

---

## 3. Impacto Cuantitativo y Comparativa de Memoria

Mediciones realizadas en **ESP32dev (Xtensa Dual-Core 240 MHz, Sin PSRAM)** controlando una **matriz HUB75 128×32**:

| Métrica | Antes de Optimizaciones | Tras Consolidación | Ganancia Neta |
| :--- | :---: | :---: | :---: |
| **DRAM Interna Libre en Reposo** | 38.120 bytes | **56.152 bytes** | **+18.032 bytes (+47.3%)** |
| **Mayor Bloque Contiguo al Arrancar** | 21.840 bytes | **51.044 bytes** | **+29.204 bytes (+133.7%)** |
| **Mayor Bloque tras Desmontaje** | 18.420 B – 27.636 B (fragmentado) | **50.000 B – 64.000 B** | **Heap unificado y continuo** |
| **Asignación DMA (8 bits vs 4 bits dinámico)** | 36.480 bytes (fijo) | **18.240 bytes (dinámico)** | **+18.240 bytes en fase TLS** |
| **Handshakes TLS por Ciclo Dashboard** | 8 a 16 handshakes individuales | **1 GET CoinGecko + 1 sesión Yahoo** | **Sesiones consolidadas** |
| **Memoria de Decodificación de Iconos** | 34.288 bytes (`PNGdec`) | **~2.500 bytes (`JPEGDEC`)** | **-92.7% de RAM consumida** |
| **Aborts OOM / Pánicos de Sistema** | 5 vectores de caída identificados | **0 abortos en prueba de carga** | **Estabilidad determinista** |

---

## 4. Estrategia de Validación y Tres Niveles de Prueba

Para garantizar que el éxito empírico se traduzca en rigor arquitectónico:

* **Nivel A — Demostrado por Código Fuente y Contratos:**
  - Eliminación de `PNGdec` (~35 KB) del flujo de memoria en ejecución.
  - Separación de `deactivate()` no bloqueante en Core 1 frente a `shutdownForDestruction()` en Core 0 (Invariantes 15 y 16).
  - Serialización JSON en flujo continuo en `ConfigLoader::saveToSD`.
* **Nivel B — Demostrado por Instrumentación de Memoria (MemTrace):**
  - Consolidación en arranque verificada: las ~70 clausuras de rutas se ubican bajo `0x3ffee000`.
  - Bloque contiguo tras desmontaje verificado: expansión del bloque contiguo a 50–64 KB tras la desactivación.
* **Nivel C — Demostrado por Carga Real en Hardware (Cualificación Física esp32dev):**
  - Validado en módulo físico ESP32 Dev (matriz 128×32 HUB75, sin PSRAM, Wi-Fi activo).
  - Rotación continua multietapa: `clock -> crypto_btc -> gif -> dashboard`.
  - Peticiones concurrentes en WebUI (`/api/system`, `/api/instances`, `/api/rotation`).

---

## 5. Resumen de Invariantes y Reglas Arquitectónicas

* **Invariante 14 (Recuperación de Recursos en Transición):** El motor saliente debe liberar completamente sus búferes transitorios antes de que el motor entrante se inicialice.
* **Invariante 15 (Desactivación Libre de Asignaciones):** `deactivate()` nunca debe realizar asignaciones dinámicas en el heap; únicamente libera, cierra y aplica el modismo swap.
* **Invariante 16 (Quiescencia en Dos Etapas: Renderizado y Recursos):** `deactivate()` en Core 1 establece la quiescencia lógica de renderizado; `shutdownForDestruction()` en Core 0 finaliza las tareas, sockets y flujos de I/O antes de liberar recursos compartidos.
* **Invariante 19 (Aislamiento de Hardware DMA):** Los búferes DMA de HUB75 se acceden exclusivamente a través de `IDrawingSurface`.
* **Invariante 21 (Aislamiento de Salida HUB75):** Durante la reconfiguración del pipeline de presentación, la señal OE se mantiene inactiva (HIGH) hasta que el primer frame válido ha sido confirmado con éxito.
