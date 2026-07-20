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

Escanea un nivel de subcarpetas dentro de la carpeta `gifs/` de tu tarjeta SD. Cada subcarpeta se
convierte en una "playlist" seleccionable en la Web UI. Por ejemplo:

```text
gifs/
  ├── mario.gif          <- siempre se reproduce, no es una playlist (archivo suelto en la raíz de gifs/)
  ├── mario/
  │   ├── walk.gif
  │   └── jump.gif
  └── sonic/
      └── run.gif
```

Aquí, `mario/` y `sonic/` se convierten en dos playlists que puedes activar/desactivar desde la
Web UI.

## Uso

Ejecuta el script apuntando ya sea a la **raíz** de tu tarjeta SD o directamente a su **carpeta
`gifs/`** - ambas opciones funcionan, el script detecta automáticamente cuál le diste:

```bash
# macOS/Linux
./generate_index.sh /Volumes/SDCARD          # raíz de la SD - desciende automáticamente a gifs/
./generate_index.sh /Volumes/SDCARD/gifs     # o la carpeta gifs/ directamente
```

```powershell
# Windows
.\generate_index.ps1 -Path E:\               # raíz de la SD - desciende automáticamente a gifs\
.\generate_index.ps1 -Path E:\gifs           # o la carpeta gifs\ directamente
```

El script siempre escribe el resultado en **`<tarjeta_sd>/gifs/playlists.json`**, la ruta exacta
que espera el firmware (`WebServerAPI.cpp` sirve `/api/playlists` leyendo
`/gifs/playlists.json` desde la tarjeta SD). Si ese archivo falta o está desactualizado, el
selector de playlists de la Web UI simplemente no mostrará nada para elegir (la reproducción de
GIFs no se ve afectada de todos modos).

**Vuelve a ejecutar el script cada vez que agregues, elimines o renombres una carpeta dentro de
`gifs/`** para que la Web UI se mantenga sincronizada con lo que realmente hay en la tarjeta SD.

---
*Esta herramienta es de código abierto y está diseñada para el ecosistema ArcadeMatrix.*
