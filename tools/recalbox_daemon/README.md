# recalbox_daemon

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

Installs a lightweight event daemon on your Recalbox or Batocera device, so ArcadeMatrix (ESP32)
can display live "now playing" marquee artwork - the same daemon protocol used by
`ArcadeMatrix_RPi` (see its `core/ssh_installer.py`), so **one daemon install serves both
projects** if you happen to run both.

Unlike the RPi project (which has a web UI with an "Install" button that SSHes in for you), the
ESP32 firmware has no such UI running on the frontend host - so this is a **standalone tool you
run from your own PC** (Windows/macOS/Linux), not from the ESP32 or from EmulationStation itself.

## What it does

1. Connects to your Recalbox/Batocera device over SSH.
2. Auto-detects which one it is (tries the Recalbox default password first, then Batocera's).
3. Uploads the matching daemon/hook script, with your **ArcadeMatrix device's IP address** baked
   in (so it knows where to publish MQTT events).
4. Reboots the device so the daemon starts automatically from now on.

Once installed, every time you launch/browse/stop a game, the device publishes a small JSON
message over MQTT (topic `recalbox/system/playing`, matching `ArcadeMatrix_RPi`'s
`core/config.py` default and ArcadeMatrix's `config.json` `[MQTT] TOPIC_RECALBOX` default):

```json
{"status": "playing", "game": "pacman", "system": "mame"}
```

`src/engines/FrontendSyncEngine.cpp` (firmware) receives this, looks for
`/pixelcade/mame/pacman.png` on the SD card (see `../pixelcade_sync/` for how to populate that
folder ahead of time), and displays it immediately - falling back to scrolling the game name as
text if no matching artwork is cached.

## Usage

### macOS / Linux

```bash
./install.sh
```

Prompts for your Recalbox/Batocera IP and your ArcadeMatrix device's IP, then does the rest.
Optional: install `sshpass` first (`brew install hudochenkov/sshpass/sshpass` on macOS, `apt
install sshpass` on Debian/Ubuntu) so the script can try the well-known default passwords
automatically. Without it, `ssh`/`scp` will just prompt you for the password interactively.

### Windows

```powershell
.\install.ps1
```

Requires the Windows 10/11 built-in OpenSSH Client (`ssh.exe`/`scp.exe`). If missing: **Settings >
Apps > Optional Features > Add a feature > OpenSSH Client**. You'll be prompted for the SSH
password interactively (try `recalboxroot` for Recalbox, `linux` for Batocera).

If PowerShell blocks the script with an execution-policy error, run it as:
```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

## Manual installation (no script, SSH in yourself)

If you'd rather not run a third-party script against your device, here's exactly what the
installers do, so you can do it by hand:

**Recalbox:**
1. `ssh root@<recalbox-ip>` (password: `recalboxroot`)
2. Edit `tools/recalbox_daemon/arcadematrix_daemon.py` locally, replacing `{{BROKER}}` with your
   ArcadeMatrix device's IP address.
3. Copy it to the device: `scp arcadematrix_daemon.py root@<recalbox-ip>:/recalbox/share/arcadematrix_daemon.py`
4. Copy the launcher: `scp "arcadematrix_launcher(permanent).sh" root@<recalbox-ip>:/recalbox/share/userscripts/`
5. `ssh root@<recalbox-ip> "chmod +x '/recalbox/share/userscripts/arcadematrix_launcher(permanent).sh' && reboot"`

**Batocera:**
1. `ssh root@<batocera-ip>` (password: `linux`)
2. Edit `arcadematrix_mqtt_batocera.sh` locally, replacing `{{BROKER}}` with your ArcadeMatrix
   device's IP address.
3. `scp arcadematrix_mqtt_batocera.sh root@<batocera-ip>:/userdata/system/scripts/arcadematrix_mqtt.sh`
4. `ssh root@<batocera-ip> "chmod +x /userdata/system/scripts/arcadematrix_mqtt.sh && reboot"`

## Files in this folder

| File | Purpose |
|---|---|
| `arcadematrix_daemon.py` | Recalbox: polls `/tmp/es_state.inf` in a loop, publishes MQTT on change. |
| `arcadematrix_launcher(permanent).sh` | Recalbox: EmulationStation startup hook that launches the daemon above. |
| `arcadematrix_mqtt_batocera.sh` | Batocera: one-shot hook invoked directly by EmulationStation per game event (no polling needed - Batocera calls this with `$1`=action `$2`=rom path `$3`=system). |
| `install.sh` | macOS/Linux automated installer. |
| `install.ps1` | Windows automated installer. |

## Related

- `../pixelcade_sync/` - populates `/pixelcade/<system>/*.png` on your SD card so artwork can be
  displayed instantly with no network access on the ESP32.
- `ArcadeMatrix_RPi/core/ssh_installer.py` - the equivalent install flow for the RPi project (same
  daemon protocol, triggered from its web UI instead of a standalone script).
