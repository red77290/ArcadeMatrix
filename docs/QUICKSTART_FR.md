# Guide de Démarrage Rapide (ESP32)

[English](QUICKSTART.md) | 🇫🇷 Français | 🇪🇸 [Español](QUICKSTART_ES.md)

## 1. Prérequis
- Une carte microSD formatée en **FAT32** ou **exFAT**.
- Un câble USB relié à votre carte ESP32 / ESP32-S3.
- Soit Chrome / Edge / Opera pour le **[Web Installer en un clic](https://red77290.github.io/ArcadeMatrix/)**, soit [VSCode](https://code.visualstudio.com/) + [Extension PlatformIO IDE](https://platformio.org/) pour compiler depuis les sources.

---

## 2. Préparation de la Carte SD
1. Formatez la carte microSD en **FAT32** ou **exFAT**.
2. Copiez le contenu du dossier `release/sdCard/` à la racine de la carte microSD.
3. Insérez la carte dans le lecteur de votre carte.

---

## 3. Flash ou Compilation du Firmware

### Option A : Flash en un clic depuis le navigateur (Le plus rapide — Aucun logiciel requis)
Ouvrez **[https://red77290.github.io/ArcadeMatrix/](https://red77290.github.io/ArcadeMatrix/)** dans Chrome, Edge ou Opera, sélectionnez votre carte (**ESP32 Standard** ou **ESP32-S3 Waveshare**) et cliquez sur **Install**.

### Option B : Compilation & Téléversement depuis les sources (PlatformIO)

#### Pour ESP32 Standard (`esp32dev`) :
```bash
pio run -e esp32dev -t upload
```

#### Pour Waveshare ESP32-S3 (`esp32s3_waveshare`) :
```bash
pio run -e esp32s3_waveshare -t upload
```

---

## 4. Connexion au Tableau de Bord Web
1. Au premier démarrage sans Wi-Fi configuré, ArcadeMatrix crée un point d'accès Wi-Fi :
   - **SSID** : `ArcadeMatrix-Setup`
   - **Mot de passe** : `matrix123`
2. Ouvrez votre navigateur sur `http://192.168.4.1` (ou `http://ArcadeMatrix.local` une fois connecté à votre réseau).
3. Configurez vos moteurs, thèmes, rotation et profitez !

---

## 5. Optionnel : Relier Recalbox / Batocera / RetroPie & Showcase en Jeu
1. **Installer le script sur la console (une seule fois)** : Lancez `tools/recalbox_daemon/install.sh` (macOS/Linux) ou `tools/recalbox_daemon/install.ps1` (Windows) en indiquant **uniquement l'IP de votre ArcadeMatrix** (la console interroge dynamiquement le DMD pour connaître le broker MQTT actif, sans jamais nécessiter de réinstallation).
2. **Activer les Marquees** : Dans la WebUI, ouvrez **Système → MQTT & API → 🎮 FrontendSync** et activez **MQTT**. La navigation dans les menus de la console affiche immédiatement les marquees Pixelcade (`0 ms`).
3. **Activer les Fiches & Vidéos de Gameplay en Jeu** (avec **[dmd_media_showcase](https://github.com/red77290/dmd_media_showcase)** sur votre réseau) :
   - Dans **🎬 DMD Media Showcase**, activez le serveur Showcase (`🟢 Communication OK`).
   - Dans **🎮 FrontendSync**, activez **Utiliser le Broker MQTT Showcase si actif** (libère automatiquement la RAM de l'ESP32) et **Showcase en Jeu (Carte + Vidéo Gameplay)**, choisissez votre **Cycle** (`Une fois`, `Alterner Marquee & Showcase` ou `Boucle`) ainsi que le **Style de Carte** (`Hériter de Showcase Studio` ou `Personnalisé sur le DMD`), puis enregistrez.
