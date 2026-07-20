# Fuentes personalizadas (`/fonts`)

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Coloca aquí archivos de fuente bitmap `.amf` para usarlos en el Reloj, la Fecha, o el mensaje
desplazante, en lugar de las ~6 fuentes compiladas en el firmware.

## Cómo obtener un archivo `.amf`

1. Consigue (o crea) fuentes bitmap `.bdf` estándar - por ejemplo las fuentes que
   `ArcadeMatrix_RPi` ya incluye en su propia carpeta `fonts/*.bdf`.
2. Copia el/los archivo(s) `.bdf` directamente en esta carpeta `/fonts` de tu tarjeta SD.
3. Ejecuta `tools/font_conversion/generate_fonts.py` apuntando a tu tarjeta SD (raíz o esta
   carpeta) - convierte por lotes todos los `.bdf` aquí a `.amf` en el mismo lugar y elimina los
   `.bdf` originales:
   ```bash
   python3 tools/font_conversion/generate_fonts.py /ruta/a/la/tarjeta/sd
   ```
   (Convertir un solo archivo a mano también es posible vía
   `tools/bdf_to_amfont/bdf_to_amfont.py mifuente.bdf mifuente.amf`, pero la herramienta de lote
   anterior es el flujo de trabajo recomendado.)

## Cómo usarlo

- **Interfaz Web (recomendado)**: Settings > Clock o Date > menú desplegable "Font". Se rellena
  en vivo mediante `GET /api/fonts`, que lista todos los archivos `.amf` encontrados aquí - sin
  necesidad de reiniciar.
- **`conf.ini`**: define `CLOCK_FONT_PATH=/fonts/mifuente.amf` bajo `[TIME]` y/o
  `DATE_FONT_PATH=/fonts/mifuente.amf` bajo `[DATE]`, o `CUSTOM_FONT_PATH=/fonts/mifuente.amf`
  bajo `[FONTS]` para el motor de mensaje desplazante.
