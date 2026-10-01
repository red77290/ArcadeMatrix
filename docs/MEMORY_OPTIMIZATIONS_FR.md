# Architecture d'Optimisation & Réclamation Mémoire : TLS & Rendu Haute-Fidélité sur ESP32 Contraint

## 1. Synthèse & Problématique Fondamentale

L'ESP32 classique (double-cœur Xtensa LX6, sans PSRAM) dispose d'environ 320 Ko de SRAM interne. Cependant, après l'initialisation du noyau FreeRTOS, des blocs de contrôle système, des piles d'interruptions et des buffers de la couche MAC Wi-Fi, il ne reste que **~60 à 80 Ko de DRAM interne** disponible pour le firmware applicatif.

Le pilotage d'une matrice LED via HUB75 DMA impose une contrainte mémoire extrême :
* Sur une **matrice 128×32** (4 096 LED RGB), une profondeur de couleur de 8 bits exige jusqu'à **~32 Ko de DRAM contiguë DMA** (sans compter les descripteurs I2S en anneau).
* Une requête HTTPS sécurisée propulsée par **mbedTLS** (requise pour la Météo, Spotify, les cours Crypto, la Bourse et les News RSS) exige **20 à 25 Ko de DRAM interne libre** pendant la négociation TLS (contexte SSL, tables de chiffrement, buffers d'entrée/sortie).
* Les architectures classiques de rendu (double-buffering DMA) réclamaient 32 Ko supplémentaires, provoquant immédiatement un crash par manque de mémoire (*Out-Of-Memory*) ou une fragmentation fatale du heap.

Grâce à un ensemble de chantiers d'optimisation étagés, ArcadeMatrix a récupéré plus de **~24 Ko de DRAM statique** et **~26 Ko de bloc contigu libre**, permettant un rendu graphique fluide à 60 FPS couplé à des opérations TLS 100 % fiables sur panneaux 128×32.

---

## 2. Optimisations Architecturales Réalisées

### 2.1 Pipeline Single DMA + Canevas Hors-Écran (`canvas_single`)

#### Le Problème
Les bibliothèques HUB75 standard allouent deux buffers DMA complets (double-buffering matériel) pour éliminer le déchirement visuel (*tearing*), consommant $2 \times 32\,\text{Ko} = 64\,\text{Ko}$ sur un affichage 128×32 en 8 bits. Cela ne laissait pratiquement aucun bloc contigu pour les tâches réseau.

#### La Solution Architecturale
ArcadeMatrix a introduit le pipeline `canvas_single` :
1. **Buffer DMA Unique :** Un seul buffer physique scanné en continu par le périphérique I2S DMA.
2. **Canevas Hors-Écran :** Les moteurs dessinent dans un canevas mémoire SRAM intermédiaire (`CanvasBufferedSurface`).
3. **Présentation Transactionnelle :** `IDrawingSurface::present()` transfère uniquement les lignes modifiées (*dirty rows*) vers le buffer DMA pendant les fenêtres de balayage sécurisées, supprimant le déchirement tout en économisant un buffer complet (~16 à 32 Ko économisés).
4. **Isolation Stricte du Matériel DMA (Invariant 19) :** Les moteurs d'affichage ne touchent jamais directement à la mémoire physique DMA (voir [ARCHITECTURE_FR.md](ARCHITECTURE_FR.md#222-invariants-architecturaux-formels-15-à-20)).

---

### 2.2 Pipeline de Présentation Dynamique & Maximiseur de Couleur Auto ($8 \leftrightarrow 4 \leftrightarrow 2$ Profondeur Adaptative)

#### Le Problème
Les animations graphiques riches (GIFs, sprites Street Fighter, bandeaux Marquee) exigent une profondeur de 8 ou 6 bits pour un rendu visuel optimal, mais leur empreinte DMA prive mbedTLS de mémoire sur le matériel contraint (ESP32 classique 128×32 sans PSRAM). À l'inverse, brider le panneau à 4 bits de manière permanente dégrade inutilement l'esthétique visuelle 100 % du temps. De plus, une remontée naïve à chaud (passer aveuglément de 4 bits à 8 bits) risque d'induire un crash immédiat par Out-Of-Memory (OOM) si la DRAM interne s'est fragmentée pendant l'exécution du moteur réseau.

#### La Solution Architecturale
Au lieu d'imposer un compromis statique au démarrage ou une bascule aveugle, le **Dynamic Presentation Pipeline** associe la quiescence matérielle à un modèle prédictif de mémoire en temps réel dans `PipelineSelectionPolicy::resolveTargetDepth` :

1. **Modélisation de la Mémoire Contiguë en Temps Réel :**
   Avant d'activer un moteur entrant, `DisplayRuntime` interroge les métriques réelles de la DRAM interne via `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. La politique calcule mathématiquement le bloc contigu résultant pour chaque profondeur candidate $D \in [2, \text{plafond}]$ :
   $$\text{estimatedBlock} = \text{currentLargestBlock} + (\text{currentDepth} - D) \times \text{bytesPerBit}$$
   où $\text{bytesPerBit} = \text{DmaMemoryLayout::calculateTotalBytes}(\text{width}, \text{height}, 1, \text{false})$.

2. **Contrat d'Admission Mémoire des Moteurs :**
   * **Moteurs Réseau TLS (Météo, Crypto, Bourse, Spotify, GNews) :** Exigent $\ge 26\,624\,\text{octets}$ (`TLS_CONTIGUOUS_REQUIRED`) plus une marge opérationnelle de sécurité de $4\,096\,\text{octets}$ ($30\,720\,\text{octets}$ au total). La politique descend les profondeurs candidates (atterrissant typiquement à 4 bits, ou 2 bits sous pression extrême) pour libérer jusqu'à **16 à 24 Ko de DRAM contiguë** immédiatement avant la négociation mbedTLS.
   * **Moteurs Graphiques (GIFs, Fighter, Clock, Canvas, Matrix) :** Exigent une marge contiguë suffisante pour les frames d'animation. La politique évalue les profondeurs depuis le plafond configuré (jusqu'à 8 bits en mode Auto) vers le bas, restaurant la fidélité maximale *uniquement si l'opération est mathématiquement protégée contre un OOM*.

3. **Plancher de Protection (Minimum 2 Bits) :**
   `FastMatrixPanel::initLuts` et les gardes de reconfiguration supportent une profondeur descendant jusqu'à 2 bits ($2 \le \text{depth} \le 8$), garantissant un mode dégradé ultra-économe en cas de fragmentation sévère du tas, sans coupure d'affichage.

4. **Transaction de Présentation Matérielle (Invariant 21) :**
   Pendant la fenêtre de transition entre moteurs, la transaction matérielle s'exécute proprement :
   1. `OE = HIGH` (Output Enable actif : écran physiquement noir).
   2. Arrêt du contrôleur I2S DMA.
   3. Libération de l'ancien buffer DMA.
   4. Allocation du nouveau buffer DMA à la profondeur cible.
   5. Régénération des LUTs de couleur via `FastMatrixPanel::initLuts(targetDepth)`.
   6. Redémarrage de l'I2S DMA.
   7. Rendu et commit de la première frame valide en mémoire hors-écran.
   8. `OE = LOW` (Output Enable relâché : reprise de l'affichage).
   * **Zéro Glitch / Invisibilité Totale :** La transaction s'exécute en **moins de 30 ms** (environ 1.5 frame à 60 FPS), totalement imperceptible lors de la transition d'engin.

---

### 2.3 Fidélité des Couleurs & Surcharge Dynamique des LUTs (`FastMatrixPanel::initLuts`)

#### Le Problème
La bibliothèque amont `ESP32-HUB75-MatrixPanel-I2S-DMA` est compilée avec des tables de conversion de luminance fixes en 8 bits (`lumConvTab_8bit`). Lorsque la profondeur de couleur est abaissée à 6, 5 ou 4 bits à chaud à l'exécution, le mappage des couleurs s'effondre :
- Les décalages de bits de plans de bits (*bitplanes*) débordent ou se désalignent, provoquant des blancs brûlés, des inversions de teintes et une forte postérisation.
- La bibliothèque amont ne recalcule pas dynamiquement ses tables de quantification internes lors d'un changement de profondeur après le boot.

#### La Solution Architecturale
`FastMatrixPanel` (héritant de `MatrixPanel_I2S_DMA` dans `src/core/MatrixEngine.cpp`) surcharge intégralement le pipeline de conversion des couleurs et d'adressage des pixels :
1. **Génération Dynamique des LUTs (`FastMatrixPanel::initLuts(uint8_t depth)`) :**
   ```cpp
   void FastMatrixPanel::initLuts(uint8_t depth) {
       uint8_t shift = 8 - depth;
       uint8_t round = (shift > 0) ? (1 << (shift - 1)) : 0;
       uint16_t maxVal = (1 << depth) - 1;
       // Pré-calcule m_lut_r[32], m_lut_g[64], m_lut_b[32] mis à l'échelle depuis la luminance 8 bits
   }
   ```
2. **Courbes Gamma & Arrondi par Canal :** Mise à l'échelle des courbes gamma 8 bits vers la profondeur cible ($2 \le \text{depth} \le 8$) avec un arrondi mathématique précis (`(val + round) >> shift`), évitant l'écrasement des tons sombres et les dérives chromatiques.
3. **Recalibration Instantanée à Chaud :** Dès que le pipeline de présentation se reconfigure ($8 \leftrightarrow 4$ ou $6 \leftrightarrow 4$), `initLuts(newDepth)` est immédiatement invoqué, garantissant un rendu colorimétrique 100 % fidèle sans aucun reboot de l'ESP32.

---

### 2.4 Quiescence Réseau Stricte & Annulation Côté Client

#### Le Problème
Si la rotation d'un moteur intervient pendant qu'une requête HTTPS est en vol, les sockets TCP résiduelles et les tâches d'arrière-plan conservent les buffers mbedTLS (~20-25 Ko), empêchant la réallocation DMA et risquant de faire crasher le moteur suivant.

#### La Solution Architecturale
* **Protocole Formel de Désactivation :** `oldEngine->deactivate()` applique un protocole d'arrêt strict en 5 phases :
  1. Signalement d'arrêt aux workers via drapeaux atomiques (`m_stopFetch = true`).
  2. Annulation forcée du transport HTTP/TLS via `session.abort()` (`_client.stop()`).
  3. Attente bloquante déterministe de fin des tâches workers (`wait workers`).
  4. Fermeture complète des handles réseau.
  5. Libération des caches JSON et cotations via l'idiome `std::swap` (Invariant 15, voir [ARCHITECTURE_FR.md](ARCHITECTURE_FR.md#222-invariants-architecturaux-formels-15-à-20)).
* **Coupure Côté Client :** L'appel immédiat à `client.stop()` détruit la socket dans LwIP. LwIP répond par un `TCP RST` à tout paquet arrivant ultérieurement du serveur et le jette sans allouer un seul octet en RAM.
* **Invariant N8 (Isolation Post-Quiescence) :** Une fois la session annulée et le composant quiescent, aucun traitement applicatif ni réallocation ne peut avoir lieu.

---

### 2.4 Allocation Paresseuse des Buffers (Marquee Raw Buffer & Décodeurs GIF)

#### Le Problème
`MarqueeEngine` allouait historiquement un buffer contigu de 8 Ko (RGB565) au démarrage pour gérer l'affichage d'icônes, même si l'utilisateur ne diffusait que du texte défilant simple.

#### La Solution Architecturale
* Transformation en **allocation paresseuse à la demande (lazy)** :
  - Pointeur initialisé à `nullptr`.
  - Alloué uniquement lorsqu'une image ou icône est explicitement requise.
  - Libéré immédiatement via `freeRawBuffer()` lors du retour au mode texte pur ou lors de la désactivation.
* Les tables de décodage GIF de `GifEngine` sont désormais allouées à la volée pendant la lecture et immédiatement réclamées à la fin.

---

### 2.5 Tâches FreeRTOS Éphémères (`SdSpace`)

#### Le Problème
La tâche de surveillance de l'espace carte SD (`SdSpace`) tournait en tâche de fond permanente, monopolisant une pile dédiée de 4 Ko plus un bloc de contrôle TCB en DRAM (~4.5 Ko au total), pour une mesure exécutée seulement toutes les quelques minutes.

#### La Solution Architecturale
* Transformation de `SdSpace` en **tâche éphémère à cycle unique** :
  - Créée à la demande lors du rafraîchissement d'espace.
  - Interroge FATFS.
  - Publie la télémétrie dans l'état global système.
  - S'auto-détruit proprement via `vTaskDelete(NULL)`, restituant immédiatement les 4 Ko de pile au heap FreeRTOS.

---

### 2.6 Optimisation de la Pile Réseau, Dimensions des Tâches & Garde-Fou de Rate-Limiting

1. **Bornes Strictes des Piles AsyncTCP et FreeRTOS :**
   - La pile de la tâche ouvrière `async_tcp` sur le Core 0 doit impérativement être maintenue à **8192 octets** (`CONFIG_ASYNC_TCP_STACK_SIZE=8192`). Réduire cette taille (ex. à 5120 octets) induit une saturation de la pile lors du traitement des connexions entrantes lourdes et de la distribution du payload WebUI compressé (~105 Ko), provoquant des timeouts de connexion navigateur (`ERR_CONNECTION_TIMED_OUT`).
   - De même, la tâche principale Arduino `loopTask` sur le Core 1 doit rester à **8192 octets** (`CONFIG_ARDUINO_LOOP_STACK_SIZE=8192`).
2. **Stabilité d'État de l'Interface Wi-Fi :**
   - Les reconfigurations du mode réseau (`WiFi.mode(WIFI_STA)`) doivent être effectuées de manière statique à l'initialisation et jamais au sein de callbacks d'événements asynchrones LwIP (comme `ARDUINO_EVENT_WIFI_STA_GOT_IP`), ce qui réinitialise l'interface réseau (`netif`) et corrompt les sockets d'écoute actives.
3. **Garde-Fou de Repli HTTP 429 et Refus Mémoire (Anti-Boucle 20 FPS) :**
   - Les moteurs d'interrogation réseau (`CryptoEngine`, `StockEngine`) doivent obligatoirement horodater leur repli sur le cache (`cache.lastFetchTime = now`) en cas d'erreur de limitation de débit HTTP (429) ou de rejet d'admission mémoire TLS. L'absence d'horodatage entraîne une boucle infinie de requêtes à chaque trame de 50 ms (20 FPS), saturant la pile de sockets LwIP et gelant le résolveur DNS.
4. **Optimisation des Buffers mDNS :**
   - Les descripteurs mDNS ne sont maintenus en mémoire que lorsque la découverte locale est activée.
5. **Calibrage Fin des Piles FreeRTOS :**
   - Mesure de l'utilisation réelle des piles via `uxTaskGetStackHighWaterMark()` :
     * `FgtLoader` (chargement Fighter) : pile réduite de 16 Ko à 8 Ko en toute sécurité (8 Ko de DRAM récupérés).
     * `weather_fetch` / `DashFetch` : ajustées à leurs besoins stricts.

---

## 3. Bilan Quantitatif & Comparatif Mémoire

Mesures relevées sur **ESP32dev (Xtensa Dual-Core 240 MHz, Sans PSRAM)** pilotant une **matrice HUB75 128×32** :

| Métrique | Avant Optimisations | Après Optimisations | Gain Net |
| :--- | :---: | :---: | :---: |
| **DRAM Interne Libre au Repos** | 38 120 octets | **62 480 octets** | **+24 360 octets (+63.9 %)** |
| **Plus Grand Bloc Contigu Libre** | 21 840 octets | **48 650 octets** | **+26 810 octets (+122.7 %)** |
| **Buffer DMA (8 bits vs 4 bits dynamique)** | 32 768 octets (fixe) | **16 384 octets (dynamique)** | **+16 384 octets en phase TLS** |
| **Mémoire des Tâches Permanentes** | ~22.5 Ko | **~10.0 Ko** | **+12.5 Ko libérés** |
| **Taux de Réussite Handshake mbedTLS** | ~35 % (OOM fréquents) | **100 % (zéro échec d'allocation)** | **Stabilité absolue** |

---

## 4. Récapitulatif des Invariants & Règles Architecturales

* **Invariant 14 (Réclamation des Ressources de Transition) :** Tout moteur sortant doit libérer intégralement ses buffers transitoires avant l'initialisation du moteur suivant.
* **Invariant 15 (Désactivation Zéro-Allocation) :** `deactivate()` ne doit jamais allouer de mémoire dynamique ; elle libère, ferme et applique l'idiome de swap.
* **Invariant 16 (Quiescence Complète de Désactivation) :** `deactivate()` ne retourne que lorsque toutes les tâches, timers et flux I/O sont terminés.
* **Invariant 19 (Isolation Stricte du Matériel DMA) :** Les framebuffers HUB75 DMA sont accédés exclusivement via `IDrawingSurface`.
* **Invariant 21 (Isolation de Sortie HUB75) :** Pendant toute la reconfiguration du pipeline de présentation, le signal OE reste fermement asservi à l'état inactif (HIGH) jusqu'au commit validé de la première frame.
