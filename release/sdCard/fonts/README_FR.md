# Polices personnalisées (`/fonts`)

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Déposez ici des fichiers de police bitmap `.amf` pour les utiliser sur l'Horloge, la Date, ou le
message défilant, à la place des ~6 polices compilées dans le firmware.

## Comment obtenir un fichier `.amf`

1. Récupérez (ou créez) une police bitmap `.bdf` standard - par exemple l'une des polices que
   `ArcadeMatrix_RPi` fournit déjà dans son propre dossier `fonts/*.bdf`.
2. Convertissez-la avec `tools/bdf_to_amfont/bdf_to_amfont.py` :
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py monfont.bdf monfont.amf
   ```
3. Copiez le fichier `monfont.amf` résultant dans ce dossier.

## Comment l'utiliser

- **Interface Web (recommandé)** : Settings > Clock ou Date > menu déroulant "Font". Il est
  peuplé en direct via `GET /api/fonts`, qui liste tous les fichiers `.amf` trouvés ici - aucun
  redémarrage nécessaire.
- **`conf.ini`** : définissez `CLOCK_FONT_PATH=/fonts/monfont.amf` sous `[TIME]` et/ou
  `DATE_FONT_PATH=/fonts/monfont.amf` sous `[DATE]`, ou `CUSTOM_FONT_PATH=/fonts/monfont.amf` sous
  `[FONTS]` pour le moteur de message défilant.
