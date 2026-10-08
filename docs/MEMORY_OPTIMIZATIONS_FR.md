# Architecture d'Optimisation & Réclamation Mémoire : TLS & Rendu Haute-Fidélité sur ESP32 Contraint

## 1. Synthèse & Problématique Fondamentale

L'ESP32 classique (double-cœur Xtensa LX6, sans PSRAM) dispose d'environ 320 Ko de SRAM interne partagée entre les instructions (IRAM), les données statiques (.data / .bss) et le tas général (DRAM). Au point d'entrée de la fonction applicative `setup()`, `heap_caps_get_free_size(MALLOC_CAP_8BIT)` rapporte **~190,5 Ko de DRAM libre initiale**, qui se contracte rapidement à **~56 à 62 Ko** une fois initialisés les buffers Wi-Fi MAC, les sockets lwIP et le serveur Web.

Le pilotage d'une matrice LED via HUB75 DMA impose une contrainte mémoire extrême :
* Sur une **matrice 128×32** (4 096 LED RGB), une profondeur de couleur de 8 bits exige jusqu'à **~36,5 Ko de DRAM contiguë DMA** (sans compter les descripteurs I2S en anneau).
* Une requête HTTPS sécurisée propulsée par **mbedTLS** (requise pour la Météo, Spotify, les cours Crypto, la Bourse et les News RSS) exige **~35 à 40 Ko de DRAM interne libre** pendant la négociation TLS (contexte SSL, tables de chiffrement et deux tampons d'enregistrement d'entrée/sortie de 16 717 octets).
* Les architectures classiques de rendu (double-buffering DMA) réclamaient 32 Ko supplémentaires, provoquant immédiatement un crash par manque de mémoire (*Out-Of-Memory*) ou une fragmentation fatale du tas.

Grâce à un ensemble de chantiers d'optimisation étagés, ArcadeMatrix a récupéré plus de **~24 Ko de DRAM statique** et **~30 Ko de bloc contigu libre**, permettant un rendu graphique fluide à 60 FPS couplé à des opérations TLS déterministes sur panneaux 128×32 sans PSRAM.

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

### 2.2 Pipeline de Présentation Dynamique & Maximiseur de Couleur Auto ($8 \leftrightarrow 7 \dots 2$ Profondeur Adaptative)

#### Le Problème
Les animations graphiques riches (GIFs, sprites de combat arcade, bandeaux Marquee, cadrans d'horloge) exigent une profondeur de 8 bits (16.7 millions de couleurs) pour un rendu visuel optimal, mais leur empreinte DMA prive mbedTLS de mémoire sur le matériel contraint (ESP32 classique 128×32 sans PSRAM). À l'inverse, brider le panneau à 4 bits de manière permanente dégrade l'esthétique visuelle 100 % du temps, tandis qu'un plafond statique à 6 bits bride inutilement les moteurs graphiques capables. De plus, une remontée naïve à chaud (passer aveuglément de 4 bits à 8 bits) risque d'induire un crash immédiat par Out-Of-Memory (OOM) si la DRAM interne s'est fragmentée pendant l'exécution du moteur réseau.

#### La Solution Architecturale
Au lieu d'imposer un compromis statique au démarrage ou une bascule aveugle, le **Dynamic Presentation Pipeline** associe la quiescence matérielle à un modèle prédictif multi-dimensionnel rigoureux dans `PipelineSelectionPolicy::resolveTargetDepth` :

1. **Évaluation Multi-Dimensionnelle de la Capacité Mathématique :**
   Entre deux créneaux de rotation (strictement après que `oldEngine->deactivate()` a achevé sa quiescence et avant que `newEngine->activate()` n'alloue), `DisplayRuntime` interroge la DRAM interne et la mémoire DMA disponibles en temps réel. Les profondeurs candidates $D \in [8 \dots 2]$ sont évaluées depuis la plus haute qualité vers le bas selon quatre bornes mathématiques :
   * **Marge de DRAM Interne Libre :**
     $$\widehat{F}(D) = \text{currentFreeInternalHeap} + (\text{currentDepth} - D) \times \text{bytesPerBit} \ge \text{SYSTEM\_MIN\_HEADROOM\_RESERVE} + \text{req.internalPersistentBytes} + \text{netReserve} + \text{audioReserve}$$
   * **Plus Grand Bloc de DRAM Contigu :**
     $$\widehat{L}(D) = \text{currentLargestBlock} + (\text{currentDepth} - D) \times \text{bytesPerBit} \ge \text{minContiguousNeeded}$$
     où $\text{minContiguousNeeded} = \text{NetworkBudget::TLS\_MIN\_COMBINED\_BLOCK}$ ($28\,672\,\text{octets}$) pour les moteurs TLS.
   * **Capacité DMA Interne :**
     $$\widehat{Dma}(D) = \text{currentFreeDma} + (\text{currentDepth} - D) \times \text{bytesPerBit} \ge \text{minDmaNeeded}$$
     où $\text{minDmaNeeded} = \text{NetworkBudget::TLS\_MIN\_FREE\_DMA}$ ($16\,384\,\text{octets}$) pour l'accélération matérielle SHA d'esp-sha.
   * **Faisabilité PSRAM :** Vérifiée pour les cartes dotées de PSRAM (ex: ESP32-S3), garantissant que le même modèle mathématique protège l'ensemble des cibles matérielles.

2. **Échelonnement Dynamique Continu ($8 \leftrightarrow 7 \leftrightarrow 6 \leftrightarrow 5 \leftrightarrow 4 \leftrightarrow 3 \leftrightarrow 2$) :**
   * **Moteurs Réseau TLS (Crypto, Bourse, Spotify, GNews, Météo) :** Rétrogradent mathématiquement vers la profondeur maximale sûre satisfaisant toutes les bornes (typiquement 4 bits, ou 2 bits sous pression extrême), libérant jusqu'à **16 à 24 Ko de DRAM contiguë** immédiatement avant la négociation mbedTLS.
   * **Moteurs Graphiques (GIFs, Fighter, Horloge, Canvas, Matrix, Marquee) :** Évalués à partir de 8 bits vers le bas, restaurant la pleine qualité **8 bits** dès que sécurisé. Sur les panneaux 128×32 et 64×32 sans PSRAM, les horloges et moteurs graphiques tournent en 8 bits natifs !
   * **Intégration WebUI :** Quand "Dynamic Presentation Pipeline" est coché, la liste déroulante manuelle de profondeur de couleur est automatiquement grisée et désactivée, avec une mention explicative indiquant que la profondeur est pilotée mathématiquement par rotation.

3. **Plancher de Protection (Minimum 2 Bits) :**
   `FastMatrixPanel::initLuts` et les gardes de reconfiguration supportent une profondeur descendant jusqu'à 2 bits ($2 \le \text{depth} \le 8$), garantissant un mode dégradé ultra-économe en cas de fragmentation sévère du tas, sans coupure d'affichage.

4. **Transaction de Présentation Matérielle (Invariant 21) :**
   Pendant la fenêtre de transition entre moteurs, la transaction matérielle s'exécute proprement :
   1. `oldEngine->deactivate()` établit la quiescence logique de rendu sur Core 1 (détachement de surface). La quiescence physique des workers/réseau est finalisée par `shutdownForDestruction()` avant démontage.
   2. `OE = HIGH` (Output Enable actif : écran physiquement noir).
   3. Démontage du pipeline DMA actif.
   4. Tentative d'allocation DMA cible (`requestedDepth` : jusqu'à 8 bits pour le graphisme, 4 bits nominal pour TLS).
   5. Si échec, tentative de repli progressif ($4 \to 2$ bits).
   6. Si tous les replis échouent, maintien de `OE = HIGH` en état `PresentationRecovery` (zéro signal parasite visible).
   7. Régénération des LUTs de couleur via `FastMatrixPanel::initLuts(effectiveDepth)`.
   8. Commit de la Frame 0 (trame noire déterministe) via `m_presentationBackend->commitFirstFrame()`.
   9. **Invariant P0 :** `OE = LOW` (Output Enable relâché) strictement après `firstFrameCommitted == true`.

---

### 2.3 Fidélité des Couleurs & LUTs Dynamiques (`FastMatrixPanel::initLuts`)

#### Le Problème
La bibliothèque amont `ESP32-HUB75-MatrixPanel-I2S-DMA` est compilée avec des tables fixes de conversion de luminance en 8 bits. Quand la profondeur de couleur est réduite à 6, 5 ou 4 bits à chaud, les décalages de bits débordent ou se désalignent, causant de la solarisation et des inversions de teintes.

#### La Solution Architecturale
`FastMatrixPanel` remplace intégralement la conversion et le dispatch des bitplanes :
1. **Génération Dynamique des LUTs (`FastMatrixPanel::initLuts(uint8_t depth)`) :**
   Recalcule les tables `m_lut_r[32]`, `m_lut_g[64]`, `m_lut_b[32]` à l'échelle de la profondeur cible.
2. **Correction Gamma par Canal :** Applique l'arrondi mathématique correct (`(val + round) >> shift`), évitant l'écrasement des tons sombres.
3. **Recalibration Instantanée :** Appelée à chaque changement de profondeur ($8 \leftrightarrow 4$ ou $6 \leftrightarrow 4$), garantissant une reproduction chromatique fidèle sans redémarrage.

---

### 2.4 Quiescence Réseau Stricte & Abort de Sockets par Domaine

#### Le Problème
Si un créneau de rotation se termine alors qu'une requête HTTPS est en cours, les sockets TCP et buffers LwIP retiennent ~35 à 40 Ko de buffers mbedTLS, empêchant la reconfiguration DMA ou provoquant un crash du moteur suivant.

#### La Solution Architecturale
* **Protocole Formel de Désactivation :** `oldEngine->deactivate()` exécute la procédure de quiescence :
  1. Signale aux workers de s'arrêter via des drapeaux atomiques (`m_stopFetch = true`).
  2. Interrompt immédiatement le transport HTTP/TLS via un abort ciblé (`net::SecureHttpClient::abortSessionsOwnedBy(ownerId)`).
  3. Effectue une attente bornée et déterministe de la fin des workers lors de la destruction sur Core 0 (`shutdownForDestruction()`).
  4. Ferme les descripteurs de transport.
  5. Purge les caches JSON et cotations via l'idiome de swap (`std::vector<T>().swap(vec)`).
* **Abort Côté Client :** L'interruption immédiate ferme le descripteur dans LwIP, qui rejette tout paquet serveur ultérieur par un TCP RST sans allouer de mémoire.

---

### 2.5 Streaming HTTP Zéro-Allocation & Buffers Statiques

#### Le Problème
Sous pression mbedTLS, mbedTLS retient ~33,4 Ko de DRAM pour ses tampons d'enregistrement. Si un parseur JSON tente d'allouer dynamiquement un `std::vector::reserve(300)` alors que le plus gros bloc est temporairement bas ($< 8\,\text{Ko}$), `operator new` lève un `std::bad_alloc`, provoquant un crash immédiat du firmware sur Core 1.

#### La Solution Architecturale
* **Allocation Fixe sur Pile :** Remplacement des redimensionnements dynamiques dans les parseurs REST par des tableaux à capacité fixe alloués sur la pile :
  ```cpp
  constexpr size_t MAX_RAW_PRICES = 320;
  float rawPrices[MAX_RAW_PRICES];
  ```
* **Zéro Contention avec mbedTLS :** Les réponses REST s'écoulent dans des cadres de pile pré-alloués sans toucher au tas, éliminant tout risque de panique mémoire.

---

### 2.6 Allocation Paresseuse des Buffers (Marquee & GIFs)

* Le buffer brut RGB565 de `MarqueeEngine` (8 Ko) est alloué **uniquement à la demande** lorsqu'une icône est présente, et libéré dès le passage en mode texte seul.
* Les tables de décodage GIF dans `GifEngine` sont allouées strictement pendant la lecture et restituées à la désactivation.

---

### 2.7 Tâches FreeRTOS Éphémères (`SdSpace`)

La tâche de surveillance de l'espace carte SD (`SdSpace`) a été transformée en **tâche éphémère à exécution unique** : instanciée à la demande, elle interroge FATFS, publie la télémétrie et s'auto-détruit immédiatement via `vTaskDelete(NULL)`, libérant ses 4 Ko de pile et son bloc TCB.

---

### 2.8 Sous-Système Réseau & Calibrage Fin des Piles

1. **Piles AsyncTCP et loopTask :** Maintenues obligatoirement à **8192 octets** pour éviter la famine de pile lors de la négociation HTTP et de l'envoi des assets WebUI compressés (105 Ko).
2. **Stabilité d'Interface Wi-Fi :** `WiFi.mode(WIFI_STA)` est exécuté statiquement au boot pour éviter de réinitialiser l'interface `netif` de LwIP en cours d'exécution.
3. **Protection HTTP 429 & Admission Mémoire :** Les moteurs réseau mettent à jour leur horodatage de dernier fetch (`cache.lastFetchTime = now`) même en cas de code 429 ou de refus mémoire, évitant les boucles de ré-interrogation à 20 FPS (50 ms).

---

### 2.9 Calcul Exact des Tampons TLS & Durcissement

#### Constat Mesuré
Avec l'affichage à 4 bits (128×32), les sessions TLS aboutissent de façon reproductible sur ESP32 classique sans PSRAM : les deux tampons mbedTLS (**16 717 octets chacun**) s'allouent tant que le plus grand bloc contigu libre atteint **50 à 64 Ko**.

#### Règles de Durcissement
* **Admission Rigoureuse :** `hasTlsRecordBufferHeadroom()` ne fait confiance à un bloc unique que si `largest >= 35 000 octets` ; sinon, elle effectue un sondage réel non-bloquant de deux blocs de 16 717 octets.
* **Délais de Connexion Bornés :** `WiFiClientSecure::connect(host, port, timeoutMs)` applique un délai strict (2 500 ms) évitant les gels de 30 secondes sur Core 1.
* **Garde Anti-Épuisement HTTP (503 Service Unavailable) :** Les routes Web modifiant la configuration vérifient `rejectIfLowHeap()` : si le plus grand bloc est $< 12\,\text{Ko}$ ou le heap interne $< 24\,\text{Ko}$, le serveur répond immédiatement un `503 + Retry-After: 2` sans allocation, évitant les crashs en cas de collision avec TLS.

---

### 2.10 Modèle Sandbox Teardown-Then-Measure (S12)

#### Le Problème : Décision sur un Tas Pollué
Auparavant, `DisplayRuntime::maybeReconfigurePipelineFor` évaluait la DRAM disponible alors que l'ancien moteur et le précédent panneau DMA étaient encore vivants en mémoire. Avec un panneau 8 bits actif, le bloc contigu mesuré chutait à $\sim 13\,\text{Ko}$, conduisant le sélecteur à rétrograder à tort en 2 bits (phénomène de battement $8 \to 4 \to 2 \to 4 \to 8$).

#### La Solution Architecturale
ArcadeMatrix dissocie strictement l'**Évaluation de Compatibilité Statique** de l'**Allocation d'Exécution** :
- **Compatibilité Statique (`ReferenceCapability`) :** Évaluée contre le profil de qualification de référence (Invariant 5), indépendamment du tas instantané.
- **Allocation d'Exécution :** Exécute la séquence déterministe **démontage puis mesure** :
  1. `oldEngine->deactivate()` libère la mémoire dynamique du moteur.
  2. Si une reconfiguration de pipeline est requise, `releasePanel()` détruit intégralement les buffers DMA HUB75 (~18 à 36 Ko).
  3. Réinitialisation inconditionnelle de `m_surface->_backend = nullptr` (évitant les pointeurs orphelins).
  4. Mesure du tas propre : le plus grand bloc contigu remonte à **50 000 – 64 000 octets**.
  5. Choix de la profondeur : 4 bits s'alloue confortablement sans aucun battement.
  6. Allocation du nouveau pipeline DMA et du canevas sur le tas sain et non fragmenté.
  7. Initialisation et activation du nouveau moteur.

---

### 2.11 Consolidation de la Disposition du Tas & Zone Système Persistante (S13)

#### Le Problème : Fragmentation par Objets "Survivants"
Dans `multi_heap`, les allocations progressent du bas vers le haut. Si des structures réseau permanentes, des piles de tâches système ou les closures de routes du serveur Web sont allouées après la surface d'affichage, elles se placent au milieu de la DRAM utilisateur (`0x3ffeab84..0x3fff4000`). En particulier, si le pilote Wi-Fi, les sockets lwIP, le client DHCP, le répondeur mDNS (avec sa pile de tâche de 4 096 octets), le client SNTP, les routes du serveur Web, AudioHub et `Core0LifecycleDispatcher` ("Lifecycle0", pile de 3 072 octets) sont alloués *après* `matrixEngine.begin()`, ils se retrouvent situés **au-dessus du buffer DMA de la matrice**. Lors du démontage ultérieur du panneau (`releasePanel()`), la mémoire DMA libérée reste **emprisonnée sous forme de trou isolé**, incapable de fusionner avec le tas libre supérieur. Le bloc contigu restait plafonné à ~22 Ko (au lieu de 60 Ko), privant `AnimatedGIF` (qui requiert 24 172 octets contigus) de son espace mémoire et forçant les panneaux 8 bits à se dégrader à 4 ou 2 bits.

#### La Solution Architecturale
Dans `AppRuntime.cpp`, toute la séquence de démarrage a été ré-ordonnée en deux zones strictement cloisonnées :
1. **Zone Système Persistante (Étape 3) :**
   - Pilote Wi-Fi, association STA, négociation DHCP et résolveurs DNS secondaires publics (1.1.1.1, 8.8.8.8).
   - Initialisation du répondeur mDNS et sa pile FreeRTOS dédiée de 4 Ko.
   - Synchronisation horaire SNTP (`configTzTime`).
   - Compilation des routes de WebServerAPI (~70 closures statiques) et tâche de travail `async_tcp` (8 192 octets).
   - État et mutex centralisés d'`AudioHub`.
   - `Core0LifecycleDispatcher` (tâche "Lifecycle0", pile de 3 072 octets).
   - **Frontière Mémoire :** L'ensemble des structures permanentes s'alloue en bas de DRAM (`0x3ffe0000..0x3ffee000`). Dès que l'Étape 3 s'achève, **plus aucune allocation permanente** n'est tolérée.
2. **Zone Sandbox Volatile (Étape 4 & Exécution) :**
   - Bitplanes DMA HUB75 (`matrixEngine.begin()`), canevas hors-écran et buffers de travail des moteurs actifs résident exclusivement dans le haut de DRAM (`0x3ffee000..0x3fffffff`).
   - Lors de la libération du panneau en rotation (`releasePanel()`), la totalité de la Sandbox se vide proprement de haut en bas, coalesçant en un bloc ininterrompu de **50 000 à 65 000 octets**.

---

### 2.12 Réconciliation du Cycle de Vie en Deux Étapes (Invariants 15 & 16)

Pour concilier les exigences temps réel de Core 1 (zéro blocage, zéro mutex, zéro allocation) avec le nettoyage complet des ressources :
* **Étape 1 — Désactivation Non-Bloquante (`deactivate()` sur Core 1) :**
  - Exécutée de façon synchrone sur Core 1 pendant `transitionSession()`.
  - Purement état : positionne `m_isActive = false` et déclenche l'interruption immédiate des sockets (`net::SecureHttpClient::abortSessionsOwnedBy(ownerId)`).
  - Zéro allocation, zéro délai de tâche, zéro attente bloquante sur Core 1.
* **Étape 2 — Quiescence Physique & Destruction (`shutdownForDestruction()` sur Core 0) :**
  - Exécutée de façon asynchrone sur Core 0 par la file de retrait des moteurs avant destruction.
  - Signale l'arrêt coopératif des workers (`DashFetch`), attend jusqu'à 300 ms leur terminaison et libère la pile de 8 192 octets sans risque de Use-After-Free (UAF).

---

### 2.13 Consolidation des Transactions Réseau & Keep-Alive TLS (S15)

#### Le Problème : Pression Répétée des Handshakes TLS
Interroger les cotations de 4 cryptos et 4 actions individuellement déclenchait jusqu'à 8 handshakes TLS consécutifs. Chaque handshake nécessitait ~35 à 40 Ko de DRAM transitoire, augmentant le risque de fragmentation et de collision avec les paquets Wi-Fi RX entrants.

#### La Solution Architecturale
* **Regroupement CoinGecko :** Interroge `/api/v3/coins/markets?vs_currency=...&symbols=...` en **1 unique requête HTTPS GET**, obtenant toutes les cryptomonnaies en un seul handshake.
* **Session Keep-Alive Yahoo Finance :** Réutilise une session persistante [`net::SecureHttpSession`](../src/core/net/SecureHttpClient.h) avec HTTP/1.1 keep-alive (`Connection: keep-alive`) pour l'ensemble des tickers boursiers. Toutes les cotations s'enchaînent sur la **même socket TLS**, réalisant **un seul handshake TLS par session de lot réussie**.
* **Consolidation sur Cache-Miss :** Lorsqu'un moteur subit un cache-miss sur un symbole, il rafraîchit en lot tous les symboles configurés dans la même session. Les rotations suivantes accèdent instantanément au cache RAM sans délai ni allocation réseau.

---

### 2.14 Hiérarchie de Cache d'Icônes à 3 Niveaux & JPEGDEC Léger (S16)

#### Le Problème : Empreinte Incurable du Décodeur PNG
`DashboardDataProvider` tentait historiquement de décoder les icônes de marché 8×8 via `PNGdec`. L'objet `PNG` embarque une fenêtre glissante zlib interne de 32 Ko (`sizeof(PNG) = 34 288 octets`). L'appel à `new PNG()` sur un ESP32 classique avec panneau 4 bits actif provoquait systématiquement un crash par `std::bad_alloc`.

#### La Solution Architecturale
ArcadeMatrix a remplacé `PNGdec` par une **Architecture de Cache d'Icônes à 3 Niveaux** coordonnée par `IconService` :
1. **L1 (Cache RAM) :** Bitmaps RGB565 décodées en mémoire (`CachedIcon`), accès instantané sans I/O.
2. **L2 (Cache Carte SD) :** Fichiers locaux persistants sous `/crypto_icons/<sym>.jpg` et `/stock_icons/<sym>.jpg`.
3. **L3 (Proxy Réseau via `images.weserv.nl`) :**
   - Le téléchargement transite par `images.weserv.nl` en **HTTP clair** (zéro empreinte TLS sur l'ESP32).
   - Le proxy transcode les icônes web en JPEG et les redimensionne.
   - Les images sont décodées via `JPEGDEC`, qui ne requiert que **~2 500 octets de heap** (soit une réduction de plus de 92 % par rapport à `PNGdec`).
* **Résilience :** Si le réseau ou le proxy est indisponible, le cache SD (L2) prend le relais ; si l'icône est absente de la SD, le moteur se replie proprement sur un affichage textuel sans icône (zéro crash).

---

### 2.15 Différé des Requêtes Réseau en Cours de Présentation Active (S17)

Sur ESP32 classique (`!psramFound()`), les requêtes météo et marchés en arrière-plan sont différées pendant que le panneau tourne en 4 bits actif une fois le premier cache initial rempli, éliminant la contention mémoire et préservant la fluidité visuelle sans dégradation fonctionnelle.

---

### 2.16 Pré-téléchargement en Fenêtre de Transition & Verrouillage TLS (S18)

#### Le Problème : Exécution TLS en Cours d'Affichage & Fuites Résiduelles
Lorsque `StockEngine` ou `CryptoEngine` basculait en mode graphique, l'absence de bougies historiques déclenchait des requêtes HTTPS directes dans `update()`. Tenter un handshake mbedTLS de 40 Ko pendant que le balayage HUB75 DMA était actif provoquait des échecs d'allocation (`MBEDTLS_ERR_MPI_ALLOC_FAILED (-16)`, `MBEDTLS_ERR_X509_ALLOC_FAILED (-10368)`). De plus, `GifEngine::deactivate()` conservait `lastPlayedGif` et `m_configuredFolders`, laissant des chaînes survivantes dans la Sandbox Zone.

#### La Solution Architecturale
1. **Pré-chargement en Fenêtre de Transition (`prefetchData()` & `fetchCombined()`) :**
   - Dans `DisplayRuntime::maybeReconfigurePipelineFor()`, avant d'allouer le nouveau panneau d'affichage, `targetEngine->prefetchData()` s'exécute dans la fenêtre où le DMA est relâché (70 à 90 Ko de mémoire libre).
   - `StockEngine::prefetchData()` et `CryptoEngine::prefetchData()` appellent `fetchCombined()` pour récupérer cotations et historique en une seule session TLS keep-alive.
   - En cours d'affichage actif, `update()` restitue les graphiques exclusivement depuis le cache RAM local, sans aucun appel TLS bloquant.
2. **Garde d'Admission TLS Stricte :**
   - `NetworkBudget::canStartTlsSession()` exige formellement `largest >= TLS_MIN_COMBINED_BLOCK` (40 Ko). Toute tentative en plein balayage actif est rejetée proprement par le budget sans solliciter mbedTLS.
3. **Purge Intégrale des Survivants à la Désactivation :**
   - `GifEngine::deactivate()` vide inconditionnellement `lastPlayedGif = String();` et appelle `std::vector<String>().swap(m_configuredFolders)`.
   - La Sandbox Zone coalesse proprement à $\ge 50\text{--}65\text{ Ko}$, permettant à `AnimatedGIF` (24 172 octets) et aux modes double-buffer 8 bits de s'allouer avec une régularité absolue.

---

## 3. Bilan Quantitatif & Comparatif Mémoire

Mesures relevées sur **ESP32dev (Xtensa Dual-Core 240 MHz, Sans PSRAM)** pilotant une **matrice HUB75 128×32** :

| Métrique | Avant Optimisations | Après Consolidation | Gain Net |
| :--- | :---: | :---: | :---: |
| **DRAM Interne Libre au Repos** | 38 120 octets | **56 152 octets** | **+18 032 octets (+47.3 %)** |
| **Plus Grand Bloc Contigu au Démarrage** | 21 840 octets | **51 044 octets** | **+29 204 octets (+133.7 %)** |
| **Plus Grand Bloc après Démontage** | 18 420 o – 27 636 o (fragmenté) | **50 000 o – 64 000 o** | **Tas unifié et coalescé** |
| **Allocation DMA (8 bits vs 4 bits dynamique)** | 36 480 octets (fixe) | **18 240 octets (dynamique)** | **+18 240 octets en phase TLS** |
| **Handshakes TLS par Cycle Dashboard** | 8 à 16 handshakes isolés | **1 GET CoinGecko + 1 session Yahoo** | **Sessions consolidées** |
| **Mémoire de Décodage d'Icône** | 34 288 octets (`PNGdec`) | **~2 500 octets (`JPEGDEC`)** | **-92.7 % de RAM consommée** |
| **Aborts OOM / Paniques Système** | 5 vecteurs de crash identifiés | **0 abort sur soak validé** | **Stabilité déterministe** |

---

## 4. Stratégie de Validation & Trois Niveaux de Preuve

Pour garantir que la réussite empirique se traduise en rigueur architecturale :

* **Niveau A — Prouvé par le Code Source & les Contrats :**
  - Élimination de `PNGdec` (~35 Ko) du chemin d'exécution mémoire.
  - Découplage `deactivate()` non-bloquant sur Core 1 vs `shutdownForDestruction()` sur Core 0 (Invariants 15 & 16).
  - Sérialisation JSON en flux dans `ConfigLoader::saveToSD`.
* **Niveau B — Prouvé par l'Instrumentation Mémoire (MemTrace) :**
  - Consolidation au boot vérifiée : toutes les closures de routes sont tassées sous `0x3ffee000`.
  - Bloc contigu de démontage vérifié : élargissement du bloc contigu à 50–64 Ko dès la désactivation.
* **Niveau C — Prouvé par Charge Réelle sur Matériel (Qualification Physique esp32dev) :**
  - Validé sur module physique ESP32 Dev (matrice 128×32 HUB75, sans PSRAM, Wi-Fi actif).
  - Rotation multi-cycles continue : `clock -> crypto_btc -> gif -> dashboard`.
  - Requêtes WebUI concurrentes (`/api/system`, `/api/instances`, `/api/rotation`).

---

## 5. Récapitulatif des Invariants & Règles Architecturales

* **Invariant 14 (Réclamation des Ressources de Transition) :** Tout moteur sortant doit libérer intégralement ses buffers transitoires avant l'initialisation du moteur suivant.
* **Invariant 15 (Désactivation Zéro-Allocation) :** `deactivate()` ne doit jamais allouer de mémoire dynamique ; elle libère, ferme et applique l'idiome de swap.
* **Invariant 16 (Quiescence en Deux Étapes : Rendu et Ressources) :** `deactivate()` sur Core 1 établit la quiescence logique de rendu ; `shutdownForDestruction()` sur Core 0 interrompt les tâches, sockets et flux I/O avant la libération des ressources partagées.
* **Invariant 19 (Isolation Stricte du Matériel DMA) :** Les framebuffers HUB75 DMA sont accédés exclusivement via `IDrawingSurface`.
* **Invariant 21 (Isolation de Sortie HUB75) :** Pendant toute la reconfiguration du pipeline de présentation, le signal OE reste fermement asservi à l'état inactif (HIGH) jusqu'au commit validé de la première frame.
