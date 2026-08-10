# ArcadeMatrix Web Installer

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Ce dossier contient les sources d'une page de flash statique [ESP Web Tools](https://esphome.github.io/esp-web-tools/), publiée sur GitHub Pages par le job `deploy-pages` de `.github/workflows/build.yml`
(exécuté à chaque push sur `main`, une fois les deux builds firmware réussis).

## Fonctionnement

- `index.html` embarque deux widgets `<esp-web-install-button>` (un par carte prise en charge), chacun
  pointant vers un petit `manifest-*.json` décrivant quel binaire doit être écrit à quel offset flash.
- Les binaires firmware réels (`firmware-esp32dev.bin`, `bootloader-esp32dev.bin`,
  `partitions-esp32dev.bin`, ainsi que les équivalents `esp32s3`) ne sont **pas** commités ici — ce sont des artefacts de build frais, copiés dans le site publié par la CI depuis `.pio/build/<env>/`. Cela garde l'historique git exempt de churn binaire à chaque commit tout en servant toujours le dernier build de `main` aux visiteurs.
- `bin/boot_app0.bin` est, lui, **commité** — c'est un minuscule fichier fixe (8KB) fourni par le framework Arduino-ESP32 (il sélectionne quelle partition d'application OTA démarrer) et il est identique pour chaque build ; il n'y a donc aucune raison de le régénérer / réhéberger à chaque build.

## Offsets flash (partitionnement par défaut Arduino-ESP32)

| Fichier | ESP32 (classique) | ESP32-S3 |
|---|---|---|
| bootloader | `0x1000` | `0x0` (l'en-tête du bootloader ROM S3 diffère) |
| partitions | `0x8000` | `0x8000` |
| boot_app0  | `0xE000` | `0xE000` |
| firmware   | `0x10000` | `0x10000` |

Cela correspond exactement à ce que `pio run -t upload -v` invoque en interne (vérifié localement avec
`esptool.py ... write_flash`) ; ainsi, un flash via navigateur depuis cette page et un flash `pio run -t upload`
produisent un résultat identique.

## Tester localement

Ouvrez `index.html` via un serveur de fichiers statique local (pas via `file://`, WebSerial a besoin d'une vraie origin)
après avoir copié dans ce dossier de vrais binaires correspondant aux chemins du manifest, par exemple :

```bash
pio run -e esp32dev -e esp32s3_waveshare
cp .pio/build/esp32dev/bootloader.bin webinstaller/bootloader-esp32dev.bin
cp .pio/build/esp32dev/partitions.bin webinstaller/partitions-esp32dev.bin
cp .pio/build/esp32dev/firmware.bin   webinstaller/firmware-esp32dev.bin
cp .pio/build/esp32s3_waveshare/bootloader.bin  webinstaller/bootloader-esp32s3_waveshare.bin
cp .pio/build/esp32s3_waveshare/partitions.bin  webinstaller/partitions-esp32s3_waveshare.bin
cp .pio/build/esp32s3_waveshare/firmware.bin    webinstaller/firmware-esp32s3_waveshare.bin
python3 scripts/validate_webinstaller.py
cd webinstaller && python3 -m http.server 8080
```

Visitez ensuite `http://localhost:8080` dans Chrome/Edge avec une carte connectée en USB.
