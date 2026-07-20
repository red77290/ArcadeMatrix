# Polices personnalisées (`/fonts`)

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Déposez ici des fichiers de police bitmap `.amf` pour les utiliser sur l'Horloge, la Date, ou le
message défilant, à la place des ~6 polices compilées dans le firmware.

## Comment obtenir un fichier `.amf`

1. Récupérez (ou créez) des polices bitmap `.bdf` standard - par exemple les polices que
   `ArcadeMatrix_RPi` fournit déjà dans son propre dossier `fonts/*.bdf`.
2. Copiez le(s) fichier(s) `.bdf` directement dans ce dossier `/fonts` de votre carte SD.
3. Lancez `tools/font_conversion/generate_fonts.py` en le pointant sur votre carte SD (racine ou
   ce dossier) - il convertit en lot tous les `.bdf` ici en `.amf`, sur place, et supprime les
   `.bdf` d'origine :
   ```bash
   python3 tools/font_conversion/generate_fonts.py /chemin/vers/carte/sd
   ```
   (Convertir un seul fichier à la main reste possible via
   `tools/bdf_to_amfont/bdf_to_amfont.py monfont.bdf monfont.amf`, mais l'outil en lot ci-dessus
   est la méthode recommandée.)

## Comment l'utiliser

- **Interface Web (recommandé)** : Settings > Clock ou Date > menu déroulant "Font". Il est
  peuplé en direct via `GET /api/fonts`, qui liste tous les fichiers `.amf` trouvés ici - aucun
  redémarrage nécessaire.
- **`conf.ini`** : définissez `CLOCK_FONT_PATH=/fonts/monfont.amf` sous `[TIME]` et/ou
  `DATE_FONT_PATH=/fonts/monfont.amf` sous `[DATE]`, ou `CUSTOM_FONT_PATH=/fonts/monfont.amf` sous
  `[FONTS]` pour le moteur de message défilant.
