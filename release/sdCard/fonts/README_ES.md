# Fuentes personalizadas (`/fonts`)

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Coloca aquí archivos de fuente bitmap `.amf` para usarlos en el Reloj, la Fecha, o el mensaje
desplazante, en lugar de las ~6 fuentes compiladas en el firmware.

## Cómo obtener un archivo `.amf`

1. Consigue (o crea) una fuente bitmap `.bdf` estándar - por ejemplo una de las mismas fuentes que
   `ArcadeMatrix_RPi` ya incluye en su propia carpeta `fonts/*.bdf`.
2. Conviértela con `tools/bdf_to_amfont/bdf_to_amfont.py`:
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py mifuente.bdf mifuente.amf
   ```
3. Copia el archivo `mifuente.amf` resultante en esta carpeta.

## Cómo usarlo

- **Interfaz Web (recomendado)**: Settings > Clock o Date > menú desplegable "Font". Se rellena
  en vivo mediante `GET /api/fonts`, que lista todos los archivos `.amf` encontrados aquí - sin
  necesidad de reiniciar.
- **`conf.ini`**: define `CLOCK_FONT_PATH=/fonts/mifuente.amf` bajo `[TIME]` y/o
  `DATE_FONT_PATH=/fonts/mifuente.amf` bajo `[DATE]`, o `CUSTOM_FONT_PATH=/fonts/mifuente.amf`
  bajo `[FONTS]` para el motor de mensaje desplazante.
