# recalbox_daemon

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Installe un daemon d'événements léger sur votre appareil Recalbox, Batocera ou RetroPie afin qu'ArcadeMatrix (ESP32)
puisse afficher en direct l'artwork marquee « now playing » — c'est le même protocole de daemon que celui utilisé par
`ArcadeMatrix_RPi` (voir son `src/core/ssh_installer.rs`), donc **une seule installation du daemon sert les deux projets** si vous utilisez les deux.

Contrairement au projet RPi (qui possède une interface web avec un bouton « Install » se connectant en SSH pour vous), le
firmware ESP32 n'a pas cette interface côté hôte frontend — il s'agit donc d'un **outil autonome que vous exécutez depuis votre propre PC** (Windows/macOS/Linux), pas depuis l'ESP32 ni depuis EmulationStation lui-même.

## Ce qu'il fait

1. Se connecte à votre appareil Recalbox/Batocera/RetroPie en SSH.
2. Sélectionne ou détecte automatiquement l'OS cible (via l'inspection de répertoires distants).
3. Upload le script daemon / hook correspondant, avec **l'adresse IP de votre DMD ArcadeMatrix** (`{{DMD_HOST}}`) intégrée. À l'exécution, le daemon interroge dynamiquement `http://<DMD_HOST>/api/mqtt/broker-target` (cache TTL de 15 s et repli automatique sur `<DMD_HOST>:1883`), ce qui permet de basculer entre le broker local du DMD et le broker partagé de **DMD Media Showcase** sans réinstaller les scripts sur la console.
4. Redémarre l'appareil afin que le daemon se lance automatiquement à partir de maintenant.

Une fois installé, chaque fois que vous lancez / parcourez / arrêtez un jeu, l'appareil publie un petit message
JSON via MQTT sur le topic standardisé `system/playing/<os>` (`system/playing/recalbox`, `system/playing/batocera` ou `system/playing/retropie`, écouté par le firmware via `system/playing/#`) :

> [!IMPORTANT]
> **Version minimale de Batocera requise :** La synchronisation dynamique des marquees pendant la navigation dans les listes de jeux et de systèmes requiert **Batocera v33 ou supérieur** (Batocera a introduit les hooks de script EmulationStation `game-selected` et `system-selected` à partir de la version 33). Sur Batocera v32 et versions antérieures, seuls les événements de lancement et d'arrêt de jeu (`game-start`, `game-end`) sont supportés. Recalbox est intégralement pris en charge sur toutes ses versions grâce à son démon de scrutation d'état natif.

```json
{"status": "playing", "game": "mslugx", "title": "Metal Slug X", "system": "neogeo"}
{"status": "browsing", "game": "pacman", "system": "mame"}
{"status": "browsing", "system": "neogeo", "type": "system"}
{"status": "stopped"}
```

- **Marquee Local Instantané (`0 ms`)** : `src/engines/FrontendSyncEngine.cpp` (firmware) reçoit l'événement, cherche `/pixelcade/<system>/<game>.png` sur la carte SD (voir `../pixelcade_sync/README_FR.md` pour savoir comment remplir ce dossier à l'avance), puis l'affiche immédiatement — avec repli sur un texte défilant ou le téléchargement de l'artwork Pixelcade s'il n'est pas en cache.
- **Double Recherche Showcase en Jeu (`title` → `fallback_query`)** : Lors de la réception d'un événement `"status": "playing"` avec le Showcase en Jeu activé, le firmware envoie `game` (basename de la ROM, ex. `"mslugx"`) en `query` et le champ optionnel `"title"` (extrait de `Game=` dans `/tmp/es_state.inf` sous Recalbox ou de l'argument `$5` `gameStart` / `$3` `game-start` sous Batocera lorsqu'il diffère de `game`, ex. `"Metal Slug X"`) en `fallback_query` avec `mode: "play_now"` et `mark_seen: false` vers `POST /api/v1/clients/{sync_id}/queue/async` (`esp32_<MAC6>_games_<WxH>_sync`).
- **Arrêt Déterministe des Conversions Serveur en Sortie de Jeu (`0` Trafic HTTP en Navigation)** : Parcourir les menus de la console (`"browsing"`) n'émet **`0` requête HTTP** vers Showcase. Dès qu'une partie active se termine (`"browsing"` ou `"stopped"`), est remplacée par le lancement d'un autre jeu, ou atteint l'échéance de préparation de `120 s`, le Core 0 envoie une unique fois `DELETE /api/v1/clients/{sync_id}/queue` (`SO_LINGER {1, 0}`) pour annuler la file et tuer immédiatement les arbres de processus de conversion serveur en cours (`dmd_show_converter`, `ffmpeg`, `yt-dlp`).

## Utilisation

### macOS / Linux

```bash
./install.sh
```

Demande l'IP de votre Recalbox/Batocera et l'IP de votre appareil ArcadeMatrix, puis fait le reste.
En option : installez `sshpass` d'abord (`brew install hudochenkov/sshpass/sshpass` sur macOS, `apt
install sshpass` sur Debian/Ubuntu) afin que le script puisse tester automatiquement les mots de passe
par défaut connus. Sans cela, `ssh` / `scp` vous demanderont simplement le mot de passe en interactif.

### Windows

```powershell
.\install.ps1
```

Nécessite le client OpenSSH intégré à Windows 10/11 (`ssh.exe` / `scp.exe`). S'il manque : **Settings >
Apps > Optional Features > Add a feature > OpenSSH Client**. Le mot de passe SSH vous sera demandé
en interactif (essayez `recalboxroot` pour Recalbox, `linux` pour Batocera).

Si PowerShell bloque le script avec une erreur d'execution policy, lancez-le ainsi :
```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

## Installation manuelle (sans script, connectez-vous vous-même en SSH)

Si vous préférez ne pas exécuter un script tiers contre votre appareil, voici exactement ce que font les installateurs, afin que vous puissiez le faire à la main :

**Recalbox :**
1. `ssh root@<recalbox-ip>` (mot de passe : `recalboxroot`)
2. Éditez `tools/recalbox_daemon/arcadematrix_daemon.py` localement, en remplaçant `{{DMD_HOST}}` (et `{{BROKER}}`) par l'adresse IP de votre DMD ArcadeMatrix.
3. Copiez-le sur l'appareil : `scp arcadematrix_daemon.py root@<recalbox-ip>:/recalbox/share/arcadematrix_daemon.py`
4. Copiez le launcher : `scp "arcadematrix_launcher(permanent).sh" root@<recalbox-ip>:/recalbox/share/userscripts/`
5. `ssh root@<recalbox-ip> "chmod +x '/recalbox/share/userscripts/arcadematrix_launcher(permanent).sh' && reboot"`

**Batocera :**
1. `ssh root@<batocera-ip>` (mot de passe : `linux`)
2. Éditez `arcadematrix_mqtt_batocera.sh` localement, en remplaçant `{{DMD_HOST}}` (et `{{BROKER}}`) par l'adresse IP de votre DMD ArcadeMatrix.
3. `scp arcadematrix_mqtt_batocera.sh root@<batocera-ip>:/userdata/system/scripts/arcadematrix_mqtt.sh`
4. `ssh root@<batocera-ip> "chmod +x /userdata/system/scripts/arcadematrix_mqtt.sh && reboot"`

## Fichiers de ce dossier

| Fichier | Rôle |
|---|---|
| `arcadematrix_daemon.py` | Recalbox : scrute `/tmp/es_state.inf` (`GamePath=`, `Game=`, `SystemId=`, `State=`) en boucle et publie sur MQTT lorsqu'un changement est détecté. |
| `arcadematrix_launcher(permanent).sh` | Recalbox : hook de démarrage EmulationStation qui lance le daemon ci-dessus. |
| `arcadematrix_mqtt_batocera.sh` | Batocera : hook one-shot appelé directement par le lanceur système (`gameStart`/`gameStop`, incluant le titre `$5`) et EmulationStation (`game-selected`, `system-selected`, `game-start`, `game-end`, incluant le titre `$3`). |
| `install.sh` | Installateur automatisé macOS/Linux. |
| `install.ps1` | Installateur automatisé Windows. |

## Liens liés

- `../pixelcade_sync/README_FR.md` - remplit `/pixelcade/<system>/*.png` sur votre carte SD afin que l'artwork puisse être affiché instantanément sans accès réseau sur l'ESP32.
- `ArcadeMatrix_RPi/core/ssh_installer.py` - le flux d'installation équivalent pour le projet RPi (même protocole de daemon, déclenché depuis son interface web au lieu d'un script autonome).
