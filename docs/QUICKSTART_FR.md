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
