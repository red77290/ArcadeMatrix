# hiscore_catalog

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

Offline compiler and reproducibility verifier for the embedded `hi2txt` high-score catalog (`_HI2TXT_CATALOG_B85`) baked directly into `arcadematrix_companion_sync.py`.

## Why embed the catalog inside `arcadematrix_companion_sync.py`?

Both `ArcadeMatrix` (`tools/recalbox_daemon/install.sh` & `install.ps1`) and `ArcadeMatrix_RPi` (`src/core/ssh_installer.rs` via `include_str!`) enforce a **strict single-file Python 3 stdlib invariant** when deploying `arcadematrix_companion_sync.py` over SSH to Recalbox, Batocera, and RetroPie:
- Zero external `.json` files or third-party binaries need to be uploaded or maintained on the console.
- Zero ESP32 flash/RAM or Core 1 impact (runs exclusively on the Linux retro-gaming host).
- Zero factory-default scores (`src/main/db_defaults` is intentionally excluded — only real binary structures from `src/main/db/*.xml` are compiled).

`build_hi2txt_catalog.py` compiles ~767 structural schemas and ~2,322 `<sameas>` ROM aliases (~3,020 resolved arcade ROMs) from [`GreatStoneEx/hi2txt-xml`](https://github.com/GreatStoneEx/hi2txt-xml) into deduplicated templates compressed with `zlib` (level 9) + `Base85` (~35 KiB), lazily decompressed in `< 5 ms` on first `.hi` lookup.

## Supported Binary Features

- **Table layouts**: `combined` (interleaved score + initials loop), `separate` (consecutive score loop and initials loop, e.g. `galaga`, `ghouls`), and `score_only` (e.g. `pacman`, `galaxian`).
- **Numeric encodings**: Packed BCD (Big-Endian & Little-Endian), unpacked BCD / nibble-skipped (`odd`/`even`), unsigned binary integers (BE/LE), byte-per-digit (`base="256"`), ASCII digits, `byte-trim` padding (e.g. `0x24` on `galaga`), `byte-skip`, `byte-swap`, `<bitmask>` byte permutations (e.g. Irem M72 `rtype`), `<sum>` multi-word scores, and `<format>` multipliers/divisors (`*10`, `*100`, `*1000`, `/100`).
- **Initials / Text encodings**: `ascii-offset`, `ascii-step`, Atari Radix-40 / Radix-32 (`base-40` / `base-32`), and partial `<charset>` remapping tables with printable ASCII fallback.

## Usage

```bash
# Recompile from GreatStoneEx/hi2txt-xml (cloned temporarily into /tmp and automatically removed)
# and update both companion script copies in ArcadeMatrix:
python3 tools/hiscore_catalog/build_hi2txt_catalog.py

# Recompile from an already cloned local directory:
python3 tools/hiscore_catalog/build_hi2txt_catalog.py --xml-repo /tmp/hi2txt-xml

# Verify embedded catalog round-trip determinism & SHA-256 parity (100% offline):
python3 tools/hiscore_catalog/build_hi2txt_catalog.py --verify
```

After updating the catalog in `ArcadeMatrix`, copy `tools/recalbox_daemon/arcadematrix_companion_sync.py` to `ArcadeMatrix_RPI/tools/arcadematrix_companion_sync.py` so all three canonical copies remain byte-for-byte identical.
