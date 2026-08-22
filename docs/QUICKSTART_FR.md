# Guide de Démarrage Rapide (ESP32)

[English](QUICKSTART.md) | 🇫🇷 Français | 🇪🇸 [Español](QUICKSTART_ES.md)

## 1. Prérequis
- [VSCode](https://code.visualstudio.com/) + [Extension PlatformIO IDE](https://platformio.org/).
- Une carte microSD formatée en FAT32 (1 Go à 32 Go).
- Un câble USB relié à votre carte ESP32.

---

## 2. Préparation de la Carte SD
1. Formatez la carte microSD en **FAT32**.
2. Copiez le contenu du dossier `sdcard_assets/` à la racine de la carte microSD.
3. Insérez la carte dans le lecteur de votre carte.

---

## 3. Compilation & Téléversement

### Pour ESP32 Standard (`esp32dev`) :
```bash
pio run -e esp32dev -t upload
```

### Pour Waveshare ESP32-S3 (`esp32s3_waveshare`) :
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
