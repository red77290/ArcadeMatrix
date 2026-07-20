# Convertisseur ArcadeMatrix BDF-vers-AMFONT

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Ce script Python (`bdf_to_amfont.py`) convertit une police bitmap **BDF** standard vers le format binaire `.amf`
chargeable à l'exécution par ArcadeMatrix, pour être utilisé avec le `BitmapFontLoader` du firmware ESP32.

## À quoi sert-il ?

Par défaut, toutes les polices utilisées par le firmware ESP32 sont compilées directement en flash au moment du build
(`src/engines/fonts/`, générées via l'outil `fontconvert` d'Adafruit). C'est efficace, mais cela signifie
qu'ajouter ou modifier une police exige une recompilation complète du firmware et un reflash.

`BitmapFontLoader` comble ce manque en chargeant une police depuis la carte SD au démarrage — aucune recompilation
nécessaire, il suffit de copier un fichier et de pointer `conf.ini` dessus. Mais l'ESP32 n'a ni parseur BDF ni moteur
de rasterisation de polices embarqué (cela coûterait de la flash / RAM / CPU que nous n'avons pas en réserve), donc les polices doivent être
**préconverties hors ligne** vers un format binaire compact que le firmware peut `malloc()` et lire
 directement. C'est exactement ce que fait ce script.

Le format BDF a été choisi comme format source parce que :
- C'est exactement le même format de police bitmap que le projet ArcadeMatrix_RPi (Raspberry Pi) livre déjà
  et charge à l'exécution (`fonts/*.bdf`, via `rgbmatrix` et `graphics.Font.LoadFont()`), donc toute police qui y fonctionne est un candidat direct.
- C'est un format strictement pixel par pixel (pas d'antialiasing / hinting / kerning à gérer), ce qui correspond
  à la manière dont les matrices LED affichent réellement — une cible simple et adaptée pour un parseur écrit from scratch.
- D'énormes bibliothèques de polices BDF gratuites existent déjà (collections BDF X11/X, polices de terminaux oldschool,
  polices pixel-art, etc.).

## Prérequis

- Python 3.6+ (bibliothèque standard uniquement, aucune dépendance externe).

## Utilisation

```bash
python3 bdf_to_amfont.py input.bdf output.amf [--first 0x20] [--last 0x7E]
```

- `--first`/`--last` limitent la plage de codepoints convertie (accepte l'hexadécimal `0x..` ou le décimal brut).
  La valeur par défaut est `0x20`-`0x7E` (ASCII imprimable), qui correspond à la plage utilisée par les polices déjà compilées dans ArcadeMatrix. Élargir la plage augmente la taille du fichier résultant (et l'utilisation RAM sur l'ESP32 une fois chargé), donc n'incluez que ce dont vous avez réellement besoin.
- Les codepoints absents de la police BDF source dans la plage demandée sont émis comme des glyphes vides, de largeur nulle, plutôt que d'interrompre la conversion.

Copiez ensuite le fichier `.amf` obtenu sur la carte SD (par ex. `/fonts/myfont.amf`) et définissez
`custom_font_path=/fonts/myfont.amf` sous `[fonts]` dans `conf.ini`. Voir `docs/DEVELOPER_FR.md` pour le workflow complet de bout en bout et le point d'intégration actuel (`MessageEngine`/`/api/message`).

## Détails du format

`.amf` reflète exactement la disposition mémoire des polices compilées d'Adafruit_GFX (`GFXfont`/`GFXglyph`), afin que
`BitmapFontLoader` puisse la reconstruire en RAM puis la transmettre directement à `matrix->setFont()` sans
traduction supplémentaire au moment du dessin :

```
Header (12 bytes):
  magic        4 bytes   "AMF1"
  first        uint16 LE first codepoint
  last         uint16 LE last codepoint
  yAdvance     uint8      newline distance (from the BDF's FONTBOUNDINGBOX)
  reserved     uint8      (unused, always 0)
  glyphCount   uint16 LE  == last - first + 1

Glyph table (9 bytes x glyphCount), one entry per codepoint in [first, last]:
  bitmapOffset uint32 LE  byte offset into the bitmap blob (glyph's OWN byte boundary)
  width        uint8
  height       uint8
  xAdvance     uint8
  xOffset      int8
  yOffset      int8

Bitmap blob: remainder of the file - packed glyph bitmaps, MSB-first, each glyph individually
             byte-aligned (i.e. NOT one continuous bitstream across glyphs) - exactly matching
             what Adafruit's own fontconvert tool produces for compiled-in fonts.
```

Les polices sont plafonnées à 65535 octets de données bitmap empaquetées (`GFXglyph.bitmapOffset` est un `uint16_t` dans
la struct d'Adafruit_GFX) — c'est la même limite qui s'applique aux polices compilées du projet, ce n'est donc
pas une limitation spécifique à `.amf`.
