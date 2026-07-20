# Convertidor ArcadeMatrix BDF-a-AMFONT

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Este script de Python (`bdf_to_amfont.py`) convierte una fuente bitmap **BDF** estándar al formato binario `.amf`
cargable en tiempo de ejecución por ArcadeMatrix, para usarlo con el `BitmapFontLoader` del firmware ESP32.

## ¿Para qué sirve?

Por defecto, todas las fuentes usadas por el firmware ESP32 se compilan directamente en la flash durante el build
(`src/engines/fonts/`, generadas mediante la herramienta `fontconvert` de Adafruit). Eso es eficiente, pero significa
que añadir o cambiar una fuente requiere una recompilación completa del firmware y volver a flashearlo.

`BitmapFontLoader` cubre ese hueco cargando una fuente desde la tarjeta SD al arrancar, sin necesidad de recompilar;
solo hay que copiar un archivo y apuntar `conf.ini` a él. Pero el ESP32 no tiene ni parser BDF ni rasterizador de
fuentes en el dispositivo (eso consumiría flash/RAM/CPU que no podemos permitirnos), así que las fuentes deben
**preconvertirse offline** a un formato binario compacto que el firmware pueda `malloc()` y leer
 directamente. Eso es exactamente lo que hace este script.

Se eligió BDF como formato de origen porque:
- Es exactamente el mismo formato de fuente bitmap que el proyecto ArcadeMatrix_RPi (Raspberry Pi) ya incluye
  y carga en tiempo de ejecución (`fonts/*.bdf`, mediante `rgbmatrix` y `graphics.Font.LoadFont()`), así que cualquier fuente que ya funcione allí es candidata directa.
- Es un formato estrictamente píxel a píxel (sin antialiasing / hinting / kerning de los que preocuparse), lo que encaja
  con cómo renderizan realmente las pantallas de matriz LED: un objetivo bueno y simple para un parser hecho desde cero.
- Ya existen enormes bibliotecas de fuentes BDF libres (colecciones BDF X11/X, fuentes de terminal oldschool,
  fuentes pixel-art, etc.).

## Requisitos previos

- Python 3.6+ (solo biblioteca estándar, sin dependencias externas).

## Uso

```bash
python3 bdf_to_amfont.py input.bdf output.amf [--first 0x20] [--last 0x7E]
```

- `--first`/`--last` restringen el rango de codepoints convertido (acepta hexadecimal `0x..` o decimal plano).
  El valor por defecto es `0x20`-`0x7E` (ASCII imprimible), que coincide con el rango usado por las fuentes ya compiladas de ArcadeMatrix. Ampliar el rango aumenta el tamaño del archivo resultante (y el uso de RAM en el ESP32 una vez cargado), así que incluye solo lo que realmente necesites.
- Los codepoints que falten en la BDF de origen dentro del rango solicitado se emiten como glifos vacíos, de ancho cero, en lugar de abortar la conversión.

Después copia el archivo `.amf` resultante a la tarjeta SD (por ejemplo `/fonts/myfont.amf`) y establece
`custom_font_path=/fonts/myfont.amf` bajo `[fonts]` en `conf.ini`. Consulta `docs/DEVELOPER_ES.md` para ver el flujo completo de extremo a extremo y el punto de integración actual (`MessageEngine`/`/api/message`).

## Detalles del formato

`.amf` refleja exactamente la disposición en memoria de las fuentes compiladas de Adafruit_GFX (`GFXfont`/`GFXglyph`), de modo que
`BitmapFontLoader` puede reconstruirla en RAM y pasarla directamente a `matrix->setFont()` sin
traducción adicional en tiempo de dibujo:

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

Las fuentes están limitadas a 65535 bytes de datos bitmap empaquetados (`GFXglyph.bitmapOffset` es un `uint16_t` en
la propia struct de Adafruit_GFX). Es el mismo límite que se aplica a las fuentes compiladas del proyecto, así que
no es una limitación específica de `.amf`.
