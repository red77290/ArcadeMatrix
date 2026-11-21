# hiscore_catalog

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Compilateur hors-ligne et vérificateur de reproductibilité du catalogue de high-scores `hi2txt` embarqué (`_HI2TXT_CATALOG_B85`) directement au sein de `arcadematrix_companion_sync.py`.

## Pourquoi embarquer le catalogue dans `arcadematrix_companion_sync.py` ?

Les deux projets `ArcadeMatrix` (`tools/recalbox_daemon/install.sh` et `install.ps1`) et `ArcadeMatrix_RPi` (`src/core/ssh_installer.rs` via `include_str!`) appliquent un **invariant strict de fichier unique Python 3 stdlib pur** lors du déploiement SSH de `arcadematrix_companion_sync.py` sur Recalbox, Batocera et RetroPie :
- Aucun fichier `.json` externe ni binaire tiers à téléverser ou maintenir sur la console.
- Zéro impact Flash/RAM ou Core 1 sur l'ESP32 (exécution exclusivement sur l'hôte Linux de la console).
- Zéro score d'usine pré-rempli (`src/main/db_defaults` est volontairement exclu — seules les structures binaires réelles de `src/main/db/*.xml` sont compilées).

`build_hi2txt_catalog.py` compile les ~767 schémas structurels et ~2 322 alias `<sameas>` (~3 020 ROMs arcade résolues) de [`GreatStoneEx/hi2txt-xml`](https://github.com/GreatStoneEx/hi2txt-xml) en templates dédupliqués compressés via `zlib` (niveau 9) + `Base85` (~35 Ko), décompressés paresseusement en `< 5 ms` lors de la première lecture d'un fichier `.hi`.

## Fonctionnalités Binaires Supportées

- **Modes de tables** : `combined` (score et initiales co-localisés dans la même boucle), `separate` (boucle de scores et boucle d'initiales distinctes, ex. `galaga`, `ghouls`), et `score_only` (ex. `pacman`, `galaxian`).
- **Encodages numériques** : BCD paqueté (Big-Endian & Little-Endian), BCD dépaqueté / filtrage de quartet (`nibble-skip` `odd`/`even`), entiers binaires non-signés (BE/LE), 1 chiffre par octet (`base="256"`), chiffres ASCII, `byte-trim` (ex. `0x24` sur `galaga`), `byte-skip`, `byte-swap`, permutations d'octets `<bitmask>` (ex. Irem M72 `rtype`), scores multi-mots `<sum>`, et opérations `<format>` (`*10`, `*100`, `*1000`, `/100`).
- **Encodages texte / initiales** : `ascii-offset`, `ascii-step`, Atari Radix-40 / Radix-32 (`base-40` / `base-32`), et tables `<charset>` partielles avec repli automatique sur l'ASCII imprimable.

## Utilisation

```bash
# Recompiler depuis GreatStoneEx/hi2txt-xml (cloné temporairement dans /tmp puis supprimé)
# et mettre à jour les deux copies du script compagnon dans ArcadeMatrix :
python3 tools/hiscore_catalog/build_hi2txt_catalog.py

# Recompiler depuis un clone local existant :
python3 tools/hiscore_catalog/build_hi2txt_catalog.py --xml-repo /tmp/hi2txt-xml

# Vérifier la reproductibilité déterministe et la parité SHA-256 (100 % hors-ligne) :
python3 tools/hiscore_catalog/build_hi2txt_catalog.py --verify
```

Après toute mise à jour du catalogue dans `ArcadeMatrix`, copiez `tools/recalbox_daemon/arcadematrix_companion_sync.py` vers `ArcadeMatrix_RPI/tools/arcadematrix_companion_sync.py` afin de conserver une parité stricte à l'octet entre les trois copies canoniques.
