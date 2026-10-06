# Modèle Mémoire & Architecture de Présentation ArcadeMatrix V4

> **Spécification d'Autorité**  
> **Statut :** Architecture Normative  
> **Cibles :** ESP32 Standard (`esp32dev`) & ESP32-S3 (`esp32s3_waveshare`)

---

## 1. Philosophie Architecturale & Domaines de Mémoire

ArcadeMatrix traite la mémoire comme une ressource dynamique et graduée par capacités. Les allocations ESP-IDF dépendent des attributs matériels :

* **DRAM Interne Générale (`MALLOC_CAP_8BIT`) :** Piles FreeRTOS, structures d'état, buffers de négociation mbedTLS.
* **DRAM Interne DMA (`MALLOC_CAP_DMA`) :** Bitplanes HUB75, buffers I2S audio, maîtres SPI.
* **PSRAM Externe (`MALLOC_CAP_SPIRAM`) :** Caches de trames GIF pré-décodées, tampons audio PCM, caches de glyphes.

> [!WARNING]
> **Vues Filtrées par Capacités (Pas de Pools Disjoints) :**  
> Sur ESP32, `MALLOC_CAP_DMA` est un attribut satisfait par un sous-ensemble de la DRAM interne. Il est **strictement interdit d'additionner** `internalFreeBytes + dmaFreeBytes`.

---

## 2. Empreinte Mémoire DMA HUB75

L'autorité de calcul est `Hub75DmaLayout::calculateBytes()` :
$$\text{Octets DMA} = \left(\frac{\text{Hauteur}}{2}\right) \times \text{Profondeur} \times (\text{Largeur} \times 2) \times \text{Buffers}$$

| Géométrie | Profondeur | Buffer Unique (`canvas_single`) | Double Buffer (`direct_double`) |
| :--- | :---: | :---: | :---: |
| **64 × 32** | 8 bits | 4 096 o | 8 192 o |
| **64 × 32** | 4 bits | **2 048 o** | 4 096 o |
| **128 × 32** | 8 bits | 16 384 o | 32 768 o |
| **128 × 32** | 4 bits | **8 192 o** | 16 384 o |
| **128 × 64** | 8 bits | 32 768 o | 65 536 o |
| **128 × 64** | 4 bits | **16 384 o** | 32 768 o |
| **256 × 64** | 8 bits | 65 536 o | 131 072 o |
| **256 × 64** | 4 bits | **32 768 o** | 65 536 o |

---

## 3. Profondeur Dynamique Déterministe ($8 \leftrightarrow 4$)

* **`configuredDepth` :** Choix utilisateur (8 bits par défaut).
* **`requestedDepth` :** Décision de rotation :
  $$\text{requestedDepth} = (\text{targetNeedsTls} \land \text{dynamicColorDepth}) \;?\; \min(\text{configuredDepth}, 4) \;:\; \text{configuredDepth}$$
* **`effectiveDepth` :** Profondeur physique réellement allouée.
* **`fallbackUsed` :** `effectiveDepth != requestedDepth`.

---

## 4. Transaction de Présentation & Invariant P0 (OE LOW)

Le signal `OE` reste fermement maintenu à `HIGH` (panneau noir) jusqu'à ce que :
1. La mémoire DMA soit allouée.
2. Les tables de couleurs (LUT) soient générées.
3. La Frame 0 soit rendue et commitée avec succès dans le contrôleur (`firstFrameCommitted == true`).
En cas d'échec catastrophique, `OE` reste à `HIGH` (`PresentationRecovery`) afin d'empêcher tout artefact visuel.
