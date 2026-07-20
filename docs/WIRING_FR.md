# Guide de câblage

🇬🇧 [English](WIRING.md) | 🇫🇷 Français | 🇪🇸 [Español](WIRING_ES.md)

Câbler une matrice LED HUB75 à un ESP32 exige des connexions précises. Les broches de données de la matrice sont mappées vers les GPIO de l'ESP32.

## Brochage ESP32 standard (WROOM)
Voici le mapping de câblage par défaut pour le moteur DMA.

| Broche HUB75 | GPIO ESP32 | Description |
|--------------|------------|-------------|
| R1        | 25         | Rouge haut |
| G1        | 26         | Vert haut |
| B1        | 27         | Bleu haut |
| R2        | 14         | Rouge bas |
| G2        | 12         | Vert bas |
| B2        | 13         | Bleu bas |
| A         | 33         | Adresse A |
| B         | 32         | Adresse B |
| C         | 22         | Adresse C |
| D         | 17         | Adresse D |
| E         | 18         | Adresse E (pour les panneaux de 64px de haut) |
| LAT (STB) | 4          | Latch |
| OE        | 15         | Output Enable |
| CLK       | 16         | Clock |

*Remarque : la broche E n'est requise que si votre matrice fait 64 pixels de haut (par ex. taux de scan 1/32).* 

## Câblage ESP32-S3

⚠️ **Conflit connu, pas encore revalidé sur vrai matériel :** le firmware utilise actuellement le **même**
mapping de broches sur ESP32-S3 que sur l'ESP32 classique (voir le tableau ci-dessus), défini une seule fois dans `src/MatrixEngine.cpp`.
C'est un problème spécifiquement pour les panneaux 256x64, car c'est précisément le cas où la PSRAM octal
(N8R8/N16R8) est requise — et la PSRAM octal sur ESP32-S3 réserve en interne **GPIO 33-37**, ce qui entre
directement en conflit avec la broche `A` (GPIO 33) et se trouve juste à côté de la broche `B` (GPIO 32) ci-dessus. Le firmware
affiche un avertissement d'exécution à ce sujet (voir `MatrixEngine::begin`), mais le mapping des broches lui-même n'a pas encore été
mis à jour / vérifié sur un câblage physique ESP32-S3. Si vous câblez un panneau 256x64 sur ESP32-S3,
vérifiez les GPIO disponibles de votre carte avant de faire confiance au mapping par défaut, et envisagez
de remapper `A`/`B` (puis de retester) vers des GPIO en dehors de la plage 33-37.

### Référence de disponibilité des GPIO ESP32-S3

Ce tableau résume quelles GPIO peuvent être utilisées sans risque pour les signaux HUB75 sur un module ESP32-S3,
selon le mode PSRAM. Vérifiez toujours la datasheet de votre carte spécifique, car certains devkits câblent aussi
des GPIO supplémentaires vers des périphériques embarqués (USB, boutons, LED RGB, etc.).

| Plage GPIO | Statut | Notes |
|------------|--------|-------|
| 0, 3, 45, 46 | **Réservé (strapping pins)** | Utilisées au boot pour la sélection du mode ; évitez de les piloter directement. |
| 19, 20 | Réservé (USB) | USB natif D-/D+ sur la plupart des devkits S3. |
| 26-32 | **Réservé (Quad Flash/PSRAM)** | Toujours réservées sur ESP32-S3, quel que soit le mode PSRAM. |
| 33-37 | **Réservé uniquement en mode PSRAM octal (« opi »)** | Libres à l'usage si votre module n'a pas de PSRAM ou utilise une PSRAM Quad (« qio »). **Entre en conflit avec la broche A actuelle du firmware (33) et est adjacente à la broche B (32) en PSRAM octal** — voir l'avertissement ci-dessus. |
| 1-2, 4-18, 21, 38-48 | Généralement libres | Pool recommandé pour remapper `A`/`B` (et tout autre signal en conflit) en dehors de 33-37 lors de l'utilisation de la PSRAM octal. |

**Action recommandée pour les builds 256x64 (PSRAM octal) sur ESP32-S3 :** remappez les broches `A` et `B` dans la struct `_pins` de `MatrixEngine.cpp` vers deux GPIO issus du pool « généralement libres » ci-dessus (par ex. 38/39),
recâblez en conséquence, puis supprimez / validez l'avertissement runtime une fois le fonctionnement confirmé sur du vrai matériel.
Cela n'a pas encore été fait dans ce codebase — considérez le câblage S3 256x64 par défaut comme **non vérifié**.
