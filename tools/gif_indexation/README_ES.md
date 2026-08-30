# Indexador de Playlists GIF de ArcadeMatrix

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Esta herramienta genera el manifiesto `playlists.json` que la interfaz Web del ESP32 necesita
para permitirte marcar/desmarcar qué subcarpetas de `gifs/` ("playlists") se incluyen en la
rotación en reposo. Solo es **necesaria para esa lista de selección de la Web UI** - la
reproducción de GIFs en sí funciona sin ella, ya que el `GifEngine` siempre lee los archivos
`.gif`/`.png`/`.raw` reales directamente desde la tarjeta SD en tiempo de ejecución.

Se proporcionan dos scripts nativos - **sin necesidad de Python**:
- `generate_index.sh` (macOS/Linux, Bash puro)
- `generate_index.ps1` (Windows, PowerShell puro)

## Qué hace

Escanea las subcarpetas dentro de las carpetas **`gifs/`** (Horizontal / YOKO) y **`gifs_tate/`** (Vertical / TATE) de tu tarjeta SD:
- Cada subcarpeta se convierte en una playlist seleccionable en la Web UI.
- Genera un archivo `index.txt` dentro de cada subcarpeta para un acceso aleatorio $O(1)$ ultra-rápido por parte del firmware.
- Genera `playlists.json` en la raíz de `gifs/` y `gifs_tate/` con el conteo de animaciones.

## Uso

Ejecuta el script apuntando ya sea a la **raíz** de tu tarjeta SD o a una carpeta específica:

```bash
# macOS/Linux
./generate_index.sh /Volumes/SDCARD          # Raíz SD - indexa tanto gifs/ (YOKO) como gifs_tate/ (TATE)
./generate_index.sh /Volumes/SDCARD/gifs     # o la carpeta gifs/ específicamente
./generate_index.sh /Volumes/SDCARD/gifs_tate# o la carpeta gifs_tate/ específicamente
```

```powershell
# Windows
.\generate_index.ps1 -Path E:\               # Raíz SD - indexa tanto gifs\ (YOKO) como gifs_tate\ (TATE)
.\generate_index.ps1 -Path E:\gifs           # o la carpeta gifs\ específicamente
.\generate_index.ps1 -Path E:\gifs_tate      # o la carpeta gifs_tate\ específicamente
```

El script genera **`<tarjeta_sd>/gifs/playlists.json`** y **`<tarjeta_sd>/gifs_tate/playlists.json`**, así como los archivos **`index.txt`** en cada subcarpeta.

**Vuelve a ejecutar el script cada vez que agregues, elimines o renombres carpetas o GIFs** para mantener sincronizada la interfaz Web y el índice del firmware.

---
*Esta herramienta es de código abierto y está diseñada para el ecosistema ArcadeMatrix.*
