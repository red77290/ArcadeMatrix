# Conversor de fuentes ArcadeMatrix (BDF → AMF)

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

`generate_fonts.py` convierte por lotes todas las fuentes bitmap `.bdf` de la carpeta `/fonts` de
tu tarjeta SD al formato binario `.amf` que puede cargar el ESP32, **en el mismo lugar** - la
misma conversión que hace `tools/bdf_to_amfont/bdf_to_amfont.py` para un solo archivo, pero
aplicada automáticamente a toda una carpeta, igual que los scripts de `tools/gif_indexation`
preprocesan los GIFs para el ESP32.

## Por qué existe esta herramienta

El `BitmapFontLoader` del firmware ESP32 (usado por los motores de Reloj, Fecha y Mensaje
desplazante para fuentes personalizadas cargadas desde la SD) no tiene analizador BDF a bordo -
solo entiende el formato compacto `.amf`. `ArcadeMatrix_RPi` distribuye y carga fuentes `.bdf`
directamente (`fonts/*.bdf`), así que para reutilizar esas mismas fuentes en tu build ESP32,
necesitan primero esta conversión offline, una sola vez.

## Qué hace

Dada la raíz de tu tarjeta SD (o directamente su carpeta `fonts`), para cada `*.bdf` encontrado:
1. Lo convierte a un archivo `.amf` con el mismo nombre (ej. `tom-thumb.bdf` → `tom-thumb.amf`).
2. **Elimina el `.bdf` de origen** una vez que el `.amf` se escribe correctamente - la tarjeta SD
   solo necesita llevar el formato que el firmware realmente puede leer.
3. Deja intacto cualquier archivo que falle al convertir y reporta el error, en lugar de eliminar
   silenciosamente una fuente de origen que funcionaba.

Los archivos `.amf` resultantes quedan inmediatamente seleccionables en la página de Ajustes de la
interfaz web (menús desplegables "Font" de Reloj/Fecha, poblados en vivo vía `GET /api/fonts`) -
sin necesidad de reiniciar. Ver `tools/bdf_to_amfont/README_ES.md` para los detalles técnicos
completos del propio formato `.amf`.

## Requisitos previos

- Python 3.6+ (solo biblioteca estándar - sin dependencias externas, no se necesita realmente
  ningún `pip install` para ejecutar `generate_fonts.py` directamente). `requirements.txt` se
  incluye solo por coherencia con las otras herramientas del proyecto que sí lo necesitan (ej.
  `mugen_extractor`); aquí está intencionalmente vacío.

## Uso

```bash
python3 generate_fonts.py <ruta_a_raiz_sd_o_carpeta_fonts>
```

Puedes pasar tanto la raíz de la tarjeta SD (se usará automáticamente una subcarpeta `fonts`) como
la propia carpeta `fonts` - misma convención que `tools/gif_indexation/generate_index.sh`.

### Scripts de ayuda para principiantes

Si prefieres no usar la línea de comandos directamente, `start_generate_fonts.sh` (macOS/Linux) y
`start_generate_fonts.bat` (Windows) crean un entorno virtual de Python aislado por ti, instalan
`requirements.txt` (un no-op aquí, pero coherente con las otras herramientas del proyecto), y
luego te piden interactivamente la ruta de tu tarjeta SD.

```bash
./start_generate_fonts.sh
```

## Ejemplo

```
$ python3 generate_fonts.py /Volumes/SDCARD
Wrote /Volumes/SDCARD/fonts/tom-thumb.amf: 95 glyphs (0 missing/blank), 176 bitmap bytes, 1233 bytes total.
Wrote /Volumes/SDCARD/fonts/5x7.amf: 95 glyphs (0 missing/blank), 475 bitmap bytes, 1532 bytes total.

Done! Converted 2 font(s) to .amf in /Volumes/SDCARD/fonts.
```

Volver a ejecutar el script en una tarjeta SD ya completamente convertida (sin `.bdf` restantes)
es seguro - simplemente indica "Nothing to do."
