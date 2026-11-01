# recalbox_daemon

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Instala un daemon de eventos ligero en tu dispositivo Recalbox, Batocera o RetroPie para que ArcadeMatrix (ESP32)
pueda mostrar en vivo el artwork marquee de « now playing »: es el mismo protocolo de daemon que usa
`ArcadeMatrix_RPi` (consulta su `src/core/ssh_installer.rs`), así que **una sola instalación del daemon sirve para ambos proyectos** si utilizas los dos.

A diferencia del proyecto RPi (que tiene una interfaz web con un botón « Install » que entra por SSH por ti), el
firmware ESP32 no dispone de esa interfaz en el host frontend, así que esto es una **herramienta independiente que ejecutas desde tu propio PC** (Windows/macOS/Linux), no desde el ESP32 ni desde EmulationStation.

## Qué hace

1. Se conecta por SSH a tu dispositivo Recalbox/Batocera/RetroPie.
2. Selecciona o detecta automáticamente el sistema operativo objetivo (mediante inspección de directorios remotos).
3. Sube el script daemon / hook correspondiente, con **la dirección IP de tu DMD ArcadeMatrix** (`{{DMD_HOST}}`) integrada. En tiempo de ejecución, el daemon consulta dinámicamente `http://<DMD_HOST>/api/mqtt/broker-target` (con caché TTL de 15 s y respaldo automático a `<DMD_HOST>:1883`) para poder alternar entre el broker local del DMD y el broker compartido de **DMD Media Showcase** sin reinstalar scripts en la consola.
4. Reinicia el dispositivo para que el daemon se inicie automáticamente a partir de entonces.

Una vez instalado, cada vez que lanzas / navegas / detienes un juego, el dispositivo publica un pequeño mensaje
JSON por MQTT en el topic estandarizado `system/playing/<os>` (`system/playing/recalbox`, `system/playing/batocera` o `system/playing/retropie`, escuchado por el firmware mediante `system/playing/#`):

> [!IMPORTANT]
> **Requisito de versión de Batocera:** La sincronización dinámica de marquesinas durante la navegación por los juegos y sistemas requiere **Batocera v33 o superior** (Batocera introdujo los hooks de script de EmulationStation `game-selected` y `system-selected` en la versión 33). En Batocera v32 y versiones anteriores, solo se admiten los eventos de inicio y parada del juego (`game-start`, `game-end`). Recalbox es totalmente compatible en todas sus versiones a través de su daemon de monitorización de estado nativo.

```json
{"status": "playing", "game": "mslugx", "title": "Metal Slug X", "system": "neogeo"}
{"status": "browsing", "game": "pacman", "system": "mame"}
{"status": "browsing", "system": "neogeo", "type": "system"}
{"status": "stopped"}
```

- **Marquesina Local Instantánea (`0 ms`)**: `src/engines/FrontendSyncEngine.cpp` (firmware) recibe el evento, busca `/pixelcade/<system>/<game>.png` en la tarjeta SD (consulta `../pixelcade_sync/README_ES.md` para ver cómo rellenar esa carpeta por adelantado) y lo muestra inmediatamente, con respaldo a texto desplazable o descarga del artwork Pixelcade si no está en caché.
- **Doble Búsqueda Showcase en Juego (`title` → `fallback_query`)**: Al recibir `"status": "playing"` con el modo Showcase en Juego activo, el firmware envía `game` (nombre base de la ROM, p. ej. `"mslugx"`) como `query` y el campo opcional `"title"` (extraído de `Game=` en `/tmp/es_state.inf` en Recalbox o del argumento `$5` `gameStart` / `$3` `game-start` en Batocera cuando difiere de `game`, p. ej. `"Metal Slug X"`) como `fallback_query` con `mode: "play_now"` y `mark_seen: false` a `POST /api/v1/clients/{sync_id}/queue/async` (`esp32_<MAC6>_games_<WxH>_sync`).
- **Aborto Determinista de Conversiones en Servidor al Salir (`0` Tráfico HTTP al Navegar)**: Desplazarse por los menús de la consola (`"browsing"`) genera **`0` peticiones HTTP** hacia Showcase. En cuanto una partida activa termina (`"browsing"` o `"stopped"`), es sustituida por otro juego o alcanza el límite de preparación de `120 s`, Core 0 envía una sola vez `DELETE /api/v1/clients/{sync_id}/queue` (`SO_LINGER {1, 0}`) para cancelar la cola y terminar al instante cualquier árbol de procesos de conversión activo en el servidor (`dmd_show_converter`, `ffmpeg`, `yt-dlp`).

## Uso

### macOS / Linux

```bash
./install.sh
```

Te pide la IP de tu Recalbox/Batocera y la IP de tu dispositivo ArcadeMatrix, y hace el resto.
Opcionalmente instala `sshpass` antes (`brew install hudochenkov/sshpass/sshpass` en macOS, `apt
install sshpass` en Debian/Ubuntu) para que el script pueda probar automáticamente las contraseñas
predeterminadas conocidas. Sin eso, `ssh` / `scp` simplemente te pedirán la contraseña de forma interactiva.

### Windows

```powershell
.\install.ps1
```

Requiere el cliente OpenSSH integrado de Windows 10/11 (`ssh.exe` / `scp.exe`). Si falta: **Settings >
Apps > Optional Features > Add a feature > OpenSSH Client**. Se te pedirá la contraseña SSH de forma
interactiva (prueba `recalboxroot` para Recalbox, `linux` para Batocera).

Si PowerShell bloquea el script con un error de execution policy, ejecútalo así:
```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

## Instalación manual (sin script, entra tú mismo por SSH)

Si prefieres no ejecutar un script de terceros contra tu dispositivo, aquí tienes exactamente lo que hacen los instaladores para que puedas hacerlo a mano:

**Recalbox:**
1. `ssh root@<recalbox-ip>` (contraseña: `recalboxroot`)
2. Edita `tools/recalbox_daemon/arcadematrix_daemon.py` localmente, sustituyendo `{{DMD_HOST}}` (y `{{BROKER}}`) por la dirección IP de tu DMD ArcadeMatrix.
3. Cópialo al dispositivo: `scp arcadematrix_daemon.py root@<recalbox-ip>:/recalbox/share/arcadematrix_daemon.py`
4. Copia el launcher: `scp "arcadematrix_launcher(permanent).sh" root@<recalbox-ip>:/recalbox/share/userscripts/`
5. `ssh root@<recalbox-ip> "chmod +x '/recalbox/share/userscripts/arcadematrix_launcher(permanent).sh' && reboot"`

**Batocera:**
1. `ssh root@<batocera-ip>` (contraseña: `linux`)
2. Edita `arcadematrix_mqtt_batocera.sh` localmente, sustituyendo `{{DMD_HOST}}` (y `{{BROKER}}`) por la dirección IP de tu DMD ArcadeMatrix.
3. `scp arcadematrix_mqtt_batocera.sh root@<batocera-ip>:/userdata/system/scripts/arcadematrix_mqtt.sh`
4. `ssh root@<batocera-ip> "chmod +x /userdata/system/scripts/arcadematrix_mqtt.sh && reboot"`

## Archivos de esta carpeta

| Archivo | Propósito |
|---|---|
| `arcadematrix_daemon.py` | Recalbox: sondea `/tmp/es_state.inf` (`GamePath=`, `Game=`, `SystemId=`, `State=`) en bucle y publica por MQTT cuando detecta cambios. |
| `arcadematrix_launcher(permanent).sh` | Recalbox: hook de arranque de EmulationStation que lanza el daemon anterior. |
| `arcadematrix_mqtt_batocera.sh` | Batocera: hook one-shot invocado directamente por el ejecutor del sistema (`gameStart`/`gameStop`, incluyendo el título `$5`) y EmulationStation (`game-selected`, `system-selected`, `game-start`, `game-end`, incluyendo el título `$3`). |
| `install.sh` | Instalador automatizado para macOS/Linux. |
| `install.ps1` | Instalador automatizado para Windows. |

## Relacionado

- `../pixelcade_sync/README_ES.md` - rellena `/pixelcade/<system>/*.png` en tu tarjeta SD para que el artwork pueda mostrarse al instante sin acceso a red en el ESP32.
- `ArcadeMatrix_RPi/core/ssh_installer.py` - el flujo de instalación equivalente para el proyecto RPi (mismo protocolo de daemon, lanzado desde su interfaz web en lugar de un script independiente).
