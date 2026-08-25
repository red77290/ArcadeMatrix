# ArcadeMatrix - Exemple de carte SD

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Ce dossier est un point de départ prêt à l'emploi pour votre carte SD : copiez son contenu à la
racine d'une carte SD formatée en **FAT32**, modifiez `config.json` pour votre Wi-Fi/matériel, et
c'est prêt à démarrer.

```
sdCard/
  ├─ config.json            <- vos réglages, voir docs/CONFIGURATION_FR.md pour la référence complète
  ├─ gifs/                <- exemple de manifeste de playlist GIF (voir gif_indexation/ ci-dessous)
  ├─ fighters_32/         <- exemple d'export de sprites MUGEN pour matrices de 32px de haut
  └─ gif_indexation/      <- outil côté PC, PAS nécessaire sur la carte SD elle-même (voir ci-dessous)
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
