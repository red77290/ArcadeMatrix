#!/usr/bin/env python3
"""
scripts/package_tools.py
Automates validation and packaging of the ArcadeMatrix PC & Console Companion Tools
bundle (ArcadeMatrix-tools.zip) attached to GitHub Releases.

Packages end-user tools (Recalbox/Batocera/RetroPie sync daemon, Pixelcade sync,
GIF playlist indexers, MUGEN sprite extractor, BDF->AMF font converters, animation
compiler, and Home Assistant blueprints) while excluding internal maintainer/cloud
infrastructure (ota-stats-worker, hiscore_catalog).
"""

import argparse
import os
import sys
import time
import zipfile

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS_DIR = os.path.join(ROOT_DIR, "tools")
DEFAULT_ZIP_PATH = os.path.join(ROOT_DIR, "ArcadeMatrix-tools.zip")
ARCHIVE_PREFIX = "ArcadeMatrix-tools"

INCLUDED_DIRS = [
    "recalbox_daemon",
    "pixelcade_sync",
    "gif_indexation",
    "mugen_extractor",
    "bdf_to_amfont",
    "font_conversion",
    "anim_compiler",
    "home_assistant",
]

INCLUDED_FILES = [
    "rpi_emulationstation_base_os_setup.sh",
]

FORBIDDEN_SUBSTRINGS = (
    "ota-stats-worker",
    "hiscore_catalog",
    "__pycache__",
)

EXCLUDE_NAMES = {
    ".DS_Store",
    "__pycache__",
    ".git",
    ".gitignore",
    "Thumbs.db",
}

EXCLUDE_SUFFIXES = (
    ".pyc",
    ".pyo",
    ".bak",
    ".swp",
)

EXECUTABLE_SUFFIXES = (
    ".sh",
    ".py",
)

REQUIRED_SOURCE_FILES = [
    "recalbox_daemon/install.sh",
    "recalbox_daemon/install.ps1",
    "recalbox_daemon/arcadematrix_daemon.py",
    "recalbox_daemon/arcadematrix_companion_sync.py",
    "pixelcade_sync/pixelcade_sync.sh",
    "pixelcade_sync/pixelcade_sync.ps1",
    "gif_indexation/generate_index.sh",
    "gif_indexation/generate_index.ps1",
    "mugen_extractor/mugen_extractor.py",
    "bdf_to_amfont/bdf_to_amfont.py",
    "font_conversion/generate_fonts.py",
    "anim_compiler/gif2amanim.py",
    "anim_compiler/requirements.txt",
    "rpi_emulationstation_base_os_setup.sh",
]

README_EN = """# ArcadeMatrix — PC & Console Companion Tools (`ArcadeMatrix-tools.zip`)

🇬🇧 **English** | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

This standalone package bundles all PC, Mac, Linux, and retro-console companion scripts for **ArcadeMatrix (ESP32 / ESP32-S3)** so you never need to clone or download the full firmware source repository.

## Included Tools

| Folder / Script | Purpose | Target Machine |
| :--- | :--- | :--- |
| [`recalbox_daemon/`](recalbox_daemon/README.md) | Real-time EmulationStation sync (`install.sh` / `install.ps1`), universal high-score extractor (~3,100 MAME/FBNeo `.hi` + Neo-Geo/CPS NVRAM), and Showcase triggers for Recalbox, Batocera, and RetroPie. | PC / Mac / Console SSH |
| [`pixelcade_sync/`](pixelcade_sync/README.md) | Downloads and organizes official Pixelcade arcade/console marquees onto your SD card (`pixelcade_sync.sh` / `pixelcade_sync.ps1`). | PC / Mac / Linux |
| [`mugen_extractor/`](mugen_extractor/README.md) | Converts MUGEN characters (`chars/`) into optimized `.fgt` RGB565 sprites + `index.txt` manifests for the `fighter` overlay. | PC / Mac / Linux (Python 3 + Pillow) |
| [`font_conversion/`](font_conversion/README.md) | Batch SD folder converter (`generate_fonts.py <SD_PATH>` / `start_generate_fonts.sh` / `.bat`) turning every `.bdf` in `SD:/fonts/` into `.amf` in-place. | PC / Mac / Linux (Python 3) |
| [`bdf_to_amfont/`](bdf_to_amfont/README.md) | Zero-dependency single-file CLI converter (`bdf_to_amfont.py input.bdf output.amf`) for Clock, Date, and Message engines. | PC / Mac / Linux (Python 3) |
| [`gif_indexation/`](gif_indexation/README.md) | Optional bulk offline playlist indexer (`generate_index.sh` / `generate_index.ps1`) when preparing large SD cards on a computer. | PC / Mac / Linux |
| `anim_compiler/` | Compiles multi-frame `.gif` animations into raw `.amanim` streams (`run_gif2amanim.sh` / `run_gif2amanim.ps1` / `gif2amanim.py`). | PC / Mac / Linux (Python 3 + Pillow) |
| `home_assistant/` | Ready-to-import Home Assistant automation blueprints for the `mqttdata` engine. | Home Assistant |
| `rpi_emulationstation_base_os_setup.sh` | Automated SSH helper for base Linux / RetroPie EmulationStation installations. | Linux / Raspberry Pi |

## Quick Links
- **ESP32 Web Installer & Portal**: https://red77290.github.io/ArcadeMatrix/
- **Main Repository & Documentation**: https://github.com/red77290/ArcadeMatrix
"""

README_FR = """# ArcadeMatrix — Outils Compagnons PC & Consoles (`ArcadeMatrix-tools.zip`)

🇬🇧 [English](README.md) | 🇫🇷 **Français** | 🇪🇸 [Español](README_ES.md)

Cette archive autonome regroupe tous les outils compagnons PC, Mac, Linux et consoles rétro pour **ArcadeMatrix (ESP32 / ESP32-S3)** afin de ne jamais avoir à télécharger l'intégralité du code source du firmware.

## Outils Inclus

| Dossier / Script | Rôle | Machine Cible |
| :--- | :--- | :--- |
| [`recalbox_daemon/`](recalbox_daemon/README_FR.md) | Synchronisation temps réel EmulationStation (`install.sh` / `install.ps1`), extracteur universel de High-Scores (~3 100 ROMs MAME/FBNeo `.hi` + NVRAM Neo-Geo/CPS) et déclenchement Showcase pour Recalbox, Batocera et RetroPie. | PC / Mac / Console SSH |
| [`pixelcade_sync/`](pixelcade_sync/README_FR.md) | Télécharge et organise les marquees officiels Pixelcade sur votre carte SD (`pixelcade_sync.sh` / `pixelcade_sync.ps1`). | PC / Mac / Linux |
| [`mugen_extractor/`](mugen_extractor/README_FR.md) | Convertit vos personnages MUGEN (`chars/`) en sprites `.fgt` RGB565 optimisés + manifeste `index.txt` pour l'overlay `fighter`. | PC / Mac / Linux (Python 3 + Pillow) |
| [`font_conversion/`](font_conversion/README_FR.md) | Convertisseur en lot de dossier SD (`generate_fonts.py <SD_PATH>` / `start_generate_fonts.sh` / `.bat`) transformant toutes les polices `.bdf` de `SD:/fonts/` en `.amf`. | PC / Mac / Linux (Python 3) |
| [`bdf_to_amfont/`](bdf_to_amfont/README_FR.md) | Convertisseur Python unitaire sans dépendance (`bdf_to_amfont.py entree.bdf sortie.amf`) pour l'Horloge, la Date et les Messages. | PC / Mac / Linux (Python 3) |
| [`gif_indexation/`](gif_indexation/README_FR.md) | Scripts optionnels d'indexation hors-ligne en lot (`generate_index.sh` / `generate_index.ps1`) pour préparer une grosse carte SD sur PC. | PC / Mac / Linux |
| `anim_compiler/` | Compile des animations `.gif` en flux binaires `.amanim` (`run_gif2amanim.sh` / `run_gif2amanim.ps1` / `gif2amanim.py`). | PC / Mac / Linux (Python 3 + Pillow) |
| `home_assistant/` | Blueprints d'automatisation Home Assistant prêts à importer pour le moteur `mqttdata`. | Home Assistant |
| `rpi_emulationstation_base_os_setup.sh` | Assistant d'installation SSH pour systèmes Linux / RetroPie avec EmulationStation. | Linux / Raspberry Pi |

## Liens Utiles
- **Web Installer & Portail ESP32** : https://red77290.github.io/ArcadeMatrix/
- **Dépôt Principal & Documentation** : https://github.com/red77290/ArcadeMatrix
"""

README_ES = """# ArcadeMatrix — Herramientas Complementarias para PC y Consolas (`ArcadeMatrix-tools.zip`)

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 **Español**

Este paquete independiente reúne todas las herramientas para PC, Mac, Linux y consolas retro de **ArcadeMatrix (ESP32 / ESP32-S3)** sin necesidad de clonar ni descargar todo el código fuente del firmware.

## Herramientas Incluidas

| Carpeta / Script | Propósito | Equipo Destino |
| :--- | :--- | :--- |
| [`recalbox_daemon/`](recalbox_daemon/README_ES.md) | Sincronización en tiempo real con EmulationStation (`install.sh` / `install.ps1`), extractor universal de récords (~3.100 ROMs MAME/FBNeo `.hi` + NVRAM Neo-Geo/CPS) y eventos Showcase para Recalbox, Batocera y RetroPie. | PC / Mac / Consola SSH |
| [`pixelcade_sync/`](pixelcade_sync/README_ES.md) | Descarga y organiza marquesinas oficiales de Pixelcade en tu tarjeta SD (`pixelcade_sync.sh` / `pixelcade_sync.ps1`). | PC / Mac / Linux |
| [`mugen_extractor/`](mugen_extractor/README_ES.md) | Convierte personajes MUGEN (`chars/`) en sprites `.fgt` RGB565 + manifiesto `index.txt` para el overlay `fighter`. | PC / Mac / Linux (Python 3 + Pillow) |
| [`font_conversion/`](font_conversion/README_ES.md) | Convertidor por lotes de carpeta SD (`generate_fonts.py <SD_PATH>` / `start_generate_fonts.sh` / `.bat`) que transforma cada `.bdf` de `SD:/fonts/` en `.amf`. | PC / Mac / Linux (Python 3) |
| [`bdf_to_amfont/`](bdf_to_amfont/README_ES.md) | Convertidor Python individual sin dependencias (`bdf_to_amfont.py entrada.bdf salida.amf`) para Reloj, Fecha y Mensaje. | PC / Mac / Linux (Python 3) |
| [`gif_indexation/`](gif_indexation/README_ES.md) | Scripts opcionales de indexación por lotes sin conexión (`generate_index.sh` / `generate_index.ps1`) para preparar tarjetas SD en PC. | PC / Mac / Linux |
| `anim_compiler/` | Compila animaciones `.gif` en flujos binarios `.amanim` (`run_gif2amanim.sh` / `run_gif2amanim.ps1` / `gif2amanim.py`). | PC / Mac / Linux (Python 3 + Pillow) |
| `home_assistant/` | Blueprints de automatización de Home Assistant listos para importar con el motor `mqttdata`. | Home Assistant |
| `rpi_emulationstation_base_os_setup.sh` | Asistente SSH automatizado para sistemas base Linux / RetroPie con EmulationStation. | Linux / Raspberry Pi |

## Enlaces Rápidos
- **Instalador Web y Portal ESP32**: https://red77290.github.io/ArcadeMatrix/
- **Repositorio Principal y Documentación**: https://github.com/red77290/ArcadeMatrix
"""


def _should_exclude(name: str) -> bool:
    if name in EXCLUDE_NAMES or name.startswith(".git"):
        return True
    return any(name.endswith(sfx) for sfx in EXCLUDE_SUFFIXES)


def validate_tools_sources() -> bool:
    print("🔍 Validating companion tools sources...")
    errors = 0

    if not os.path.isdir(TOOLS_DIR):
        print(f"❌ Missing tools directory: {TOOLS_DIR}")
        return False

    for d in INCLUDED_DIRS:
        dp = os.path.join(TOOLS_DIR, d)
        if not os.path.isdir(dp):
            print(f"❌ Missing required tools subdirectory: {dp}")
            errors += 1

    for req_rel in REQUIRED_SOURCE_FILES:
        fp = os.path.join(TOOLS_DIR, req_rel)
        if not os.path.isfile(fp):
            print(f"❌ Missing required companion tool file: {fp}")
            errors += 1
        elif os.path.getsize(fp) == 0:
            print(f"❌ Companion tool file is empty (0 bytes): {fp}")
            errors += 1

    # Verify Home Assistant blueprints directory has at least one YAML file
    ha_dir = os.path.join(TOOLS_DIR, "home_assistant")
    yaml_found = False
    if os.path.isdir(ha_dir):
        for _r, _dirs, files in os.walk(ha_dir):
            if any(f.endswith((".yaml", ".yml")) for f in files):
                yaml_found = True
                break
    if not yaml_found:
        print(f"❌ No Home Assistant YAML blueprints found in: {ha_dir}")
        errors += 1

    if errors == 0:
        print("  ✓ All companion tools source files are present and valid.")
        return True
    return False


def _write_bytes_to_zip(
    zf: zipfile.ZipFile,
    arcname: str,
    raw_bytes: bytes,
    executable: bool = False,
    mtime: float | None = None,
) -> None:
    dt = time.localtime(mtime if mtime is not None else time.time())[:6]
    zinfo = zipfile.ZipInfo(filename=arcname, date_time=dt)
    zinfo.compress_type = zipfile.ZIP_DEFLATED
    zinfo.create_system = 3  # UNIX permissions compatibility
    mode = 0o100755 if executable else 0o100644
    zinfo.external_attr = mode << 16
    zf.writestr(zinfo, raw_bytes)


def _add_file_to_zip(zf: zipfile.ZipFile, src_path: str, rel_inside_prefix: str) -> int:
    arcname = f"{ARCHIVE_PREFIX}/{rel_inside_prefix.replace(os.sep, '/')}"
    with open(src_path, "rb") as f:
        data = f.read()

    is_exec = src_path.endswith(EXECUTABLE_SUFFIXES)
    if is_exec:
        # Normalize CRLF -> LF on shell and Python scripts so console/Linux execution never fails
        data = data.replace(b"\r\n", b"\n")

    _write_bytes_to_zip(
        zf,
        arcname=arcname,
        raw_bytes=data,
        executable=is_exec,
        mtime=os.path.getmtime(src_path),
    )
    return len(data)


def package_tools_zip(output_zip_path: str = DEFAULT_ZIP_PATH) -> bool:
    print(f"📦 Packaging companion tools bundle into: {output_zip_path}...")
    file_count = 0
    total_bytes = 0

    os.makedirs(os.path.dirname(os.path.abspath(output_zip_path)), exist_ok=True)

    with zipfile.ZipFile(output_zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        # 1. Root trilingual index READMEs inside ArcadeMatrix-tools/
        for fname, content in (
            ("README.md", README_EN),
            ("README_FR.md", README_FR),
            ("README_ES.md", README_ES),
        ):
            payload = content.encode("utf-8")
            _write_bytes_to_zip(zf, f"{ARCHIVE_PREFIX}/{fname}", payload, executable=False)
            file_count += 1
            total_bytes += len(payload)

        # 2. Standalone top-level scripts
        for rel_file in sorted(INCLUDED_FILES):
            src_path = os.path.join(TOOLS_DIR, rel_file)
            total_bytes += _add_file_to_zip(zf, src_path, rel_file)
            file_count += 1

        # 3. Curated companion subdirectories
        for subdir in sorted(INCLUDED_DIRS):
            subdir_path = os.path.join(TOOLS_DIR, subdir)
            for root, dirs, files in os.walk(subdir_path):
                dirs[:] = sorted(d for d in dirs if not _should_exclude(d))
                for fname in sorted(files):
                    if _should_exclude(fname):
                        continue
                    full_path = os.path.join(root, fname)
                    rel_path = os.path.relpath(full_path, TOOLS_DIR)
                    total_bytes += _add_file_to_zip(zf, full_path, rel_path)
                    file_count += 1

    zip_size_kb = os.path.getsize(output_zip_path) / 1024.0
    print(
        f"  ✓ Packaged {file_count} files "
        f"({total_bytes / 1024.0:.1f} KB uncompressed, {zip_size_kb:.1f} KB zip)."
    )

    print("🔍 Verifying companion tools archive integrity...")
    with zipfile.ZipFile(output_zip_path, "r") as zf:
        namelist = set(zf.namelist())
        expected_entries = [
            f"{ARCHIVE_PREFIX}/README.md",
            f"{ARCHIVE_PREFIX}/README_FR.md",
            f"{ARCHIVE_PREFIX}/README_ES.md",
            *[f"{ARCHIVE_PREFIX}/{rel}" for rel in REQUIRED_SOURCE_FILES],
        ]
        for exp in expected_entries:
            if exp not in namelist:
                print(f"❌ Zip missing required entry: {exp}")
                return False

        for entry in namelist:
            if any(bad in entry for bad in FORBIDDEN_SUBSTRINGS):
                print(f"❌ Zip contains forbidden internal/cache path: {entry}")
                return False

        bad_file = zf.testzip()
        if bad_file:
            print(f"❌ Corrupted file in zip: {bad_file}")
            return False

    print("🎉 Companion tools packaging & verification PASSED.")
    return True


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Package and validate ArcadeMatrix PC & Console Companion Tools bundle."
    )
    parser.add_argument("--output", default=DEFAULT_ZIP_PATH, help="Output zip file path")
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help="Only validate source companion tools without creating the zip file",
    )
    args = parser.parse_args()

    if not validate_tools_sources():
        print("💥 Companion tools source validation failed.")
        sys.exit(1)

    if args.verify_only:
        print("🎉 Verification complete.")
        sys.exit(0)

    if not package_tools_zip(args.output):
        print("💥 Companion tools packaging failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
