# Pipeline de Preprocesamiento de Assets de ArcadeMatrix

Este documento describe el pipeline de optimización de assets fuera de línea para ArcadeMatrix ESP32.

---

## 💡 ¿Por qué preprocesar assets fuera de línea?

El ESP32 es un microcontrolador de alto rendimiento, pero su memoria RAM y reloj de CPU son limitados en comparación con un PC o Raspberry Pi.

En lugar de hacer que el ESP32 decodifique formatos de imagen pesados o scripts de animación complejos en tiempo real, ArcadeMatrix adopta una filosofía estricta: **Preprocesar fuera de línea en PC, leer ultra rápido en transmisión directa en el ESP32**.

---

## 🕹️ 1. Sprites MUGEN (`.fgt`)

El script en Python `tools/mugen_extractor/mugen_extractor.py` convierte los archivos de personajes MUGEN (`.sff`, `.air`) en archivos binarios optimizados `.fgt`.

- **Extracción**: Decodificación de paleta maestra y selección de animaciones clave (`walk`, `attack`, `hit`, `win`, `special1-3`, `super1-3`, `fall`).
- **Suelo Virtual (Virtual Ground)**: Calcula una línea de suelo uniforme (`ground_y`) para evitar temblores en las animaciones durante los ataques.
- **Escalado (`--scale`)**: Factor de escala personalizado (ej. `--scale 0.5` para reducir a la mitad y ahorrar un 75% de memoria RAM).
- **Indexación**: Genera `index.txt` (leído por `FighterEngine.cpp` en ESP32) e `index.json` (leído por Raspberry Pi).

```bash
# Ejemplo de conversión para paneles de 64px a escala 0.5:
python3 tools/mugen_extractor/mugen_extractor.py --src /ruta/mugen/chars --dest /Volumes/SDCARD/fighters_64 --scale 0.5
```
---

## 🔤 2. Fuentes Bitmap Personalizadas (`.amf`)

El script `tools/bdf_to_amfont/bdf_to_amfont.py` convierte fuentes bitmap estándar `.bdf` en el formato binario compacto `.amf` (ArcadeMatrix Font).

- **Ganancia de rendimiento**: Decodificación binaria instantánea sin parseador BDF textual en RAM.
- **Uso**: Copia tus archivos `.bdf` en la carpeta `/fonts/` de la tarjeta SD y ejecuta el script. Las fuentes `.amf` generadas aparecen de inmediato en los desplegables de la WebUI para Reloj y Fecha.

```bash
python3 tools/bdf_to_amfont/bdf_to_amfont.py /Volumes/SDCARD
```

---

## 🎬 3. Listas de reproducción y Animaciones GIF (Horizontales y Verticales)

- **Almacenamiento por orientación**:
  - 🖥️ **Horizontal (YOKO)**: `/gifs/<carpeta>/` (ej. `/gifs/arcade`, `/gifs/nintendo`).
  - 📱 **Vertical (TATE)**: `/gifs_tate/<carpeta>/` (ej. `/gifs_tate/shmup`, `/gifs_tate/pinball`).
- **Cambio automático de resolución**: El firmware reproduce automáticamente animaciones verticales en pantallas verticales/retrato, y horizontales en pantallas apaisadas.
- **Indexación**: `tools/gif_indexation/generate_index.sh` (o `.ps1`) genera los archivos `index.txt` en cada subcarpeta para un acceso aleatorio $O(1)$ ultra-rápido, así como los manifiestos `playlists.json` para la Web UI.

---

## 🖼️ 4. Marquees en formato crudo RGB565 (`.raw`)

- **Formato**: Píxeles crudos en RGB565 little-endian (exactamente `ancho * alto * 2` bytes).
- **Uso**: Mostrar fondos de escenarios de combate (`SD:/fighters_32/backgrounds/stage1.raw`) o enviar marquees en directo a través de la API REST `POST /api/marquee`.
