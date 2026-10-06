# ArcadeMatrix - Exemple de carte SD

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Ce dossier est un point de départ prêt à l'emploi pour votre carte SD : copiez son contenu à la
racine d'une carte SD formatée en **FAT32**, modifiez les fichiers de configuration modulaires dans `config/` (ou le fichier hérité `config.json`), et c'est prêt à démarrer.

```
sdCard/
  ├─ config/                <- réglages modulaires par domaine (v4.0+)
  │    ├─ hardware.json     <- dimensions de matrice, profondeur de couleur, driver, pins
  │    ├─ system.json       <- fuseau horaire, mode nuit, luminosité, langue
  │    ├─ network.json      <- identifiants Wi-Fi et paramètres MQTT
  │    ├─ playlist.json     <- séquence de rotation des moteurs et durées
  │    └─ instances/        <- configurations individuelles d'instances (*.json)
  ├─ config.json            <- fallback monolithique hérité (migré automatiquement vers config/ si présent)
  ├─ gifs/                  <- exemple de manifeste de playlist GIF (voir gif_indexation/ ci-dessous)
  ├─ gifs_tate/             <- bibliothèque verticale (Tate) (ex. échantillon 32x128 pour panneaux pivotés)
  ├─ fighters_32/           <- exemple d'export de sprites MUGEN pour matrices de 32px de haut
  ├─ fighters_64/           <- exemple d'export de sprites MUGEN pour matrices de 64px de haut
  ├─ fonts/                 <- polices bitmap personnalisées
  └─ gif_indexation/        <- outil côté PC, PAS nécessaire sur la carte SD elle-même (voir ci-dessous)
```

## À propos de `gif_indexation/`
Ce sous-dossier est une copie de confort de `tools/gif_indexation/` du dépôt principal - les
scripts qui régénèrent `gifs/playlists.json` après ajout/suppression de dossiers de GIF. **Ils
s'exécutent sur votre ordinateur (macOS/Linux/Windows), pas sur l'ESP32**, donc vous n'avez pas
strictement besoin de copier ce sous-dossier sur la carte SD - il est fourni ici uniquement pour
que vous ayez tout dans un seul téléchargement sans avoir à cloner le dépôt complet. Voir
`gif_indexation/README_FR.md` pour l'utilisation.

---
*Pour le guide de mise en place complet, consultez `docs/GETTING_STARTED_FR.md` du dépôt principal.*
