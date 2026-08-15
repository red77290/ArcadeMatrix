# Guide de Configuration (`conf.ini` & API)

🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 Français | 🇪🇸 [Español](CONFIGURATION_ES.md)

ArcadeMatrix est entièrement configurable sans recompilation. Tous les paramètres peuvent être gérés via l'interface Web (`http://arcadematrix.local`), l'API REST ou dans le fichier `conf.ini` à la racine de la carte SD.

---

## 📌 Remarques Importantes

- **`ROTATION`** : La chaîne `ROTATION` contrôle les modules autonomes (`clock`, `date`, `weather`, `gifs`, `crypto`, `stocks`, `temp`, `decibel`). **Note : `sprites` n'est PAS un module de rotation**. Les combattants MUGEN sont dessinés en surimpression par `FighterEngine` via `FIGHTER_ENABLED`.
- **Précision Audio & Visualiseur** :
  - `VisualizerEngine` calcule un **pseudo-spectre** de bandes d'énergie d'amplitude optimisé pour l'affichage matriciel LED en temps réel.
  - `DecibelEngine` fournit un **indicateur relatif de niveau sonore étalonnable** calculé sur l'énergie RMS du microphone.
