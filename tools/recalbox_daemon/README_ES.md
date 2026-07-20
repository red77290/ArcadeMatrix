# recalbox_daemon

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Instala un daemon de eventos ligero en tu dispositivo Recalbox o Batocera para que ArcadeMatrix (ESP32)
pueda mostrar en vivo el artwork marquee de « now playing »: es el mismo protocolo de daemon que usa
`ArcadeMatrix_RPi` (consulta su `core/ssh_installer.py`), así que **una sola instalación del daemon sirve para ambos proyectos** si utilizas los dos.

A diferencia del proyecto RPi (que tiene una interfaz web con un botón « Install » que entra por SSH por ti), el
firmware ESP32 no dispone de esa interfaz en el host frontend, así que esto es una **herramienta independiente que ejecutas desde tu propio PC** (Windows/macOS/Linux), no desde el ESP32 ni desde EmulationStation.

## Qué hace

1. Se conecta por SSH a tu dispositivo Recalbox/Batocera.
2. Detecta automáticamente cuál de los dos es (prueba primero la contraseña predeterminada de Recalbox y luego la de Batocera).
3. Sube el script daemon / hook correspondiente, con **la dirección IP de tu dispositivo ArcadeMatrix** integrada
   (para que sepa dónde publicar los eventos MQTT).
4. Reinicia el dispositivo para que el daemon se inicie automáticamente a partir de entonces.

Una vez instalado, cada vez que lanzas / navegas / detienes un juego, el dispositivo publica un pequeño mensaje
JSON por MQTT (topic `recalbox/system/playing`, igual al valor por defecto de `core/config.py` en
`ArcadeMatrix_RPi` y de `TOPIC_RECALBOX` en la sección `[MQTT]` de `conf.ini`):

```json
{"status": "playing", "game": "pacman", "system": "mame"}
```

`src/engines/RetroFrontendListener.cpp` (firmware) lo recibe, busca
`/pixelcade/mame/pacman.png` en la tarjeta SD (consulta `../pixelcade_sync/README_ES.md` para ver cómo rellenar esa
carpeta por adelantado) y lo muestra inmediatamente, con fallback a texto desplazable con el nombre del juego si no hay artwork correspondiente cacheado.

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
2. Edita `tools/recalbox_daemon/arcadematrix_daemon.py` localmente, sustituyendo `{{BROKER}}` por la dirección IP de tu dispositivo ArcadeMatrix.
3. Cópialo al dispositivo: `scp arcadematrix_daemon.py root@<recalbox-ip>:/recalbox/share/arcadematrix_daemon.py`
4. Copia el launcher: `scp "arcadematrix_launcher(permanent).sh" root@<recalbox-ip>:/recalbox/share/userscripts/`
5. `ssh root@<recalbox-ip> "chmod +x '/recalbox/share/userscripts/arcadematrix_launcher(permanent).sh' && reboot"`

**Batocera:**
1. `ssh root@<batocera-ip>` (contraseña: `linux`)
2. Edita `arcadematrix_mqtt_batocera.sh` localmente, sustituyendo `{{BROKER}}` por la dirección IP de tu dispositivo ArcadeMatrix.
3. `scp arcadematrix_mqtt_batocera.sh root@<batocera-ip>:/userdata/system/scripts/arcadematrix_mqtt.sh`
4. `ssh root@<batocera-ip> "chmod +x /userdata/system/scripts/arcadematrix_mqtt.sh && reboot"`

## Archivos de esta carpeta

| Archivo | Propósito |
|---|---|
| `arcadematrix_daemon.py` | Recalbox: sondea `/tmp/es_state.inf` en bucle y publica por MQTT cuando detecta cambios. |
| `arcadematrix_launcher(permanent).sh` | Recalbox: hook de arranque de EmulationStation que lanza el daemon anterior. |
| `arcadematrix_mqtt_batocera.sh` | Batocera: hook one-shot invocado directamente por EmulationStation en cada evento de juego (sin polling; Batocera lo llama con `$1`=action `$2`=rom path `$3`=system). |
| `install.sh` | Instalador automatizado para macOS/Linux. |
| `install.ps1` | Instalador automatizado para Windows. |

## Relacionado

- `../pixelcade_sync/README_ES.md` - rellena `/pixelcade/<system>/*.png` en tu tarjeta SD para que el artwork pueda mostrarse al instante sin acceso a red en el ESP32.
- `ArcadeMatrix_RPi/core/ssh_installer.py` - el flujo de instalación equivalente para el proyecto RPi (mismo protocolo de daemon, lanzado desde su interfaz web en lugar de un script independiente).
