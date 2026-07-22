# Convertisseur de polices ArcadeMatrix (BDF → AMF)

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

`generate_fonts.py` convertit en lot toutes les polices bitmap `.bdf` du dossier `/fonts` de votre
carte SD vers le format binaire `.amf` chargeable par l'ESP32, **en place** - la même conversion
que `tools/bdf_to_amfont/bdf_to_amfont.py` effectue pour un seul fichier, mais appliquée
automatiquement à tout un dossier, comme les scripts de `tools/gif_indexation` pré-traitent les
GIFs pour l'ESP32.

## Pourquoi cet outil existe

Le `BitmapFontLoader` du firmware ESP32 (utilisé par les moteurs Horloge, Date et Message
défilant pour les polices personnalisées chargées depuis la SD) n'a aucun parseur BDF embarqué -
il ne comprend que le format compact `.amf`. `ArcadeMatrix_RPi` fournit et charge directement des
polices `.bdf` (`fonts/*.bdf`), donc pour réutiliser ces mêmes polices sur votre build ESP32,
elles nécessitent d'abord cette conversion hors ligne, une fois pour toutes.

## Ce qu'il fait

Étant donné la racine de votre carte SD (ou directement son dossier `fonts`), pour chaque `*.bdf`
trouvé :
1. Le convertit en un fichier `.amf` de même nom (ex. `tom-thumb.bdf` → `tom-thumb.amf`).
2. **Supprime le `.bdf` source** une fois le `.amf` écrit avec succès - la carte SD ne doit
   transporter que le format que le firmware peut réellement lire.
3. Laisse intact tout fichier dont la conversion échoue et signale l'erreur, plutôt que de
   supprimer silencieusement une police source qui fonctionnait.

Les fichiers `.amf` résultants deviennent immédiatement sélectionnables dans la page Settings de
l'interface Web (menus déroulants "Font" de l'Horloge/Date, peuplés en direct via
`GET /api/fonts`) - sans redémarrage nécessaire. Voir `tools/bdf_to_amfont/README_FR.md` pour les
détails techniques complets du format `.amf` lui-même.

## Prérequis

- Python 3.6+ (bibliothèque standard uniquement - aucune dépendance externe, aucun
  `pip install` réellement nécessaire pour exécuter `generate_fonts.py` directement).
  `requirements.txt` est inclus uniquement par cohérence avec les autres outils du projet qui en
  ont réellement besoin (ex. `mugen_extractor`) ; il est volontairement vide ici.

## Utilisation

```bash
python3 generate_fonts.py <chemin_vers_racine_sd_ou_dossier_fonts>
```

Vous pouvez passer soit la racine de la carte SD (un sous-dossier `fonts` sera utilisé
automatiquement), soit le dossier `fonts` lui-même - même convention que
`tools/gif_indexation/generate_index.sh`.

### Wrappers pour débutants

Si vous préférez ne pas utiliser la ligne de commande directement, `start_generate_fonts.sh`
(macOS/Linux) et `start_generate_fonts.bat` (Windows) créent pour vous un environnement virtuel
Python isolé, installent `requirements.txt` (un no-op ici, mais cohérent avec les autres outils du
projet), puis vous demandent interactivement le chemin de votre carte SD.

```bash
./start_generate_fonts.sh
```

## Exemple

```
$ python3 generate_fonts.py /Volumes/SDCARD
Wrote /Volumes/SDCARD/fonts/tom-thumb.amf: 95 glyphs (0 missing/blank), 176 bitmap bytes, 1233 bytes total.
Wrote /Volumes/SDCARD/fonts/5x7.amf: 95 glyphs (0 missing/blank), 475 bitmap bytes, 1532 bytes total.

Done! Converted 2 font(s) to .amf in /Volumes/SDCARD/fonts.
```

Relancer le script sur une carte SD déjà entièrement convertie (plus aucun `.bdf` restant) est
sans danger - il indique simplement "Nothing to do."
