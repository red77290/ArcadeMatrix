# hiscore_catalog

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Compilador sin conexión y verificador de reproducibilidad del catálogo integrado de récords `hi2txt` (`_HI2TXT_CATALOG_B85`) incrustado directamente en `arcadematrix_companion_sync.py`.

## ¿Por qué incrustar el catálogo dentro de `arcadematrix_companion_sync.py`?

Tanto `ArcadeMatrix` (`tools/recalbox_daemon/install.sh` e `install.ps1`) como `ArcadeMatrix_RPi` (`src/core/ssh_installer.rs` mediante `include_str!`) aplican una **invariante estricta de archivo único Python 3 stdlib puro** al desplegar `arcadematrix_companion_sync.py` por SSH en Recalbox, Batocera y RetroPie:
- Cero archivos `.json` externos ni binarios adicionales que subir o mantener en la consola.
- Cero impacto en Flash/RAM o Core 1 del ESP32 (se ejecuta exclusivamente en el host Linux de la consola retro).
- Cero puntuaciones de fábrica predefinidas (`src/main/db_defaults` se excluye deliberadamente; solo se compilan las estructuras binarias reales de `src/main/db/*.xml`).

`build_hi2txt_catalog.py` compila ~767 esquemas estructurales y ~2.322 alias `<sameas>` (~3.020 ROMs arcade resueltas) de [`GreatStoneEx/hi2txt-xml`](https://github.com/GreatStoneEx/hi2txt-xml) en plantillas deduplicadas comprimidas con `zlib` (nivel 9) + `Base85` (~35 KiB), descomprimidas bajo demanda en `< 5 ms` al leer el primer archivo `.hi`.

## Características Binarias Soportadas

- **Modos de tabla**: `combined` (puntuación e iniciales intercaladas en el mismo bucle), `separate` (bucle de puntuaciones y bucle de iniciales separados, p. ej. `galaga`, `ghouls`) y `score_only` (p. ej. `pacman`, `galaxian`).
- **Codificaciones numéricas**: BCD empaquetado (Big-Endian y Little-Endian), BCD desempaquetado / filtrado de nibbles (`nibble-skip` `odd`/`even`), enteros binarios sin signo (BE/LE), dígito por byte (`base="256"`), dígitos ASCII, relleno `byte-trim` (p. ej. `0x24` en `galaga`), `byte-skip`, `byte-swap`, permutaciones de bytes `<bitmask>` (p. ej. Irem M72 `rtype`), sumas multi-palabra `<sum>` y operaciones `<format>` (`*10`, `*100`, `*1000`, `/100`).
- **Codificaciones de texto / iniciales**: `ascii-offset`, `ascii-step`, Atari Radix-40 / Radix-32 (`base-40` / `base-32`) y tablas `<charset>` parciales con respaldo automático a ASCII imprimible.

## Uso

```bash
# Recompilar desde GreatStoneEx/hi2txt-xml (clonado temporalmente en /tmp y eliminado al terminar)
# y actualizar ambas copias del script compañero en ArcadeMatrix:
python3 tools/hiscore_catalog/build_hi2txt_catalog.py

# Recompilar desde un clon local existente:
python3 tools/hiscore_catalog/build_hi2txt_catalog.py --xml-repo /tmp/hi2txt-xml

# Verificar la reproducibilidad determinista y la paridad SHA-256 (100 % sin conexión):
python3 tools/hiscore_catalog/build_hi2txt_catalog.py --verify
```

Tras actualizar el catálogo en `ArcadeMatrix`, copia `tools/recalbox_daemon/arcadematrix_companion_sync.py` a `ArcadeMatrix_RPI/tools/arcadematrix_companion_sync.py` para mantener la paridad exacta byte a byte entre las tres copias canónicas.
