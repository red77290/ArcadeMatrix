# recalbox_daemon

🇬🇧 [English](README.md) | 🇫🇷 Français | 🇪🇸 [Español](README_ES.md)

Installe un daemon d'événements léger sur votre appareil Recalbox ou Batocera afin qu'ArcadeMatrix (ESP32)
puisse afficher en direct l'artwork marquee « now playing » — c'est le même protocole de daemon que celui utilisé par
`ArcadeMatrix_RPi` (voir son `core/ssh_installer.py`), donc **une seule installation du daemon sert les deux projets** si vous utilisez les deux.

Contrairement au projet RPi (qui possède une interface web avec un bouton « Install » se connectant en SSH pour vous), le
firmware ESP32 n'a pas cette interface côté hôte frontend — il s'agit donc d'un **outil autonome que vous exécutez depuis votre propre PC** (Windows/macOS/Linux), pas depuis l'ESP32 ni depuis EmulationStation lui-même.

## Ce qu'il fait

1. Se connecte à votre appareil Recalbox/Batocera en SSH.
2. Détecte automatiquement lequel des deux c'est (essaie d'abord le mot de passe Recalbox par défaut, puis celui de Batocera).
3. Upload le script daemon / hook correspondant, avec **l'adresse IP de votre appareil ArcadeMatrix** intégrée
   (afin qu'il sache où publier les événements MQTT).
4. Redémarre l'appareil afin que le daemon se lance automatiquement à partir de maintenant.

Une fois installé, chaque fois que vous lancez / parcourez / arrêtez un jeu, l'appareil publie un petit message
JSON via MQTT (topic `recalbox/system/playing`, identique à la valeur par défaut de `core/config.py` dans
`ArcadeMatrix_RPi` et à `TOPIC_RECALBOX` dans la section `[MQTT]` de `config.json`) :

```json
{"status": "playing", "game": "pacman", "system": "mame"}
```

`src/engines/FrontendSyncEngine.cpp` (firmware) le reçoit, cherche
`/pixelcade/mame/pacman.png` sur la carte SD (voir `../pixelcade_sync/README_FR.md` pour savoir comment remplir ce
dossier à l'avance), puis l'affiche immédiatement — avec fallback sur un texte défilant du nom du jeu si aucun artwork correspondant n'est en cache.

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
2. Éditez `tools/recalbox_daemon/arcadematrix_daemon.py` localement, en remplaçant `{{BROKER}}` par l'adresse IP de votre appareil ArcadeMatrix.
3. Copiez-le sur l'appareil : `scp arcadematrix_daemon.py root@<recalbox-ip>:/recalbox/share/arcadematrix_daemon.py`
4. Copiez le launcher : `scp "arcadematrix_launcher(permanent).sh" root@<recalbox-ip>:/recalbox/share/userscripts/`
5. `ssh root@<recalbox-ip> "chmod +x '/recalbox/share/userscripts/arcadematrix_launcher(permanent).sh' && reboot"`

**Batocera :**
1. `ssh root@<batocera-ip>` (mot de passe : `linux`)
2. Éditez `arcadematrix_mqtt_batocera.sh` localement, en remplaçant `{{BROKER}}` par l'adresse IP de votre appareil ArcadeMatrix.
3. `scp arcadematrix_mqtt_batocera.sh root@<batocera-ip>:/userdata/system/scripts/arcadematrix_mqtt.sh`
4. `ssh root@<batocera-ip> "chmod +x /userdata/system/scripts/arcadematrix_mqtt.sh && reboot"`

## Fichiers de ce dossier

| Fichier | Rôle |
|---|---|
| `arcadematrix_daemon.py` | Recalbox : scrute `/tmp/es_state.inf` en boucle et publie sur MQTT lorsqu'un changement est détecté. |
| `arcadematrix_launcher(permanent).sh` | Recalbox : hook de démarrage EmulationStation qui lance le daemon ci-dessus. |
| `arcadematrix_mqtt_batocera.sh` | Batocera : hook one-shot appelé directement par EmulationStation à chaque événement de jeu (pas de polling nécessaire — Batocera l'appelle avec `$1`=action `$2`=rom path `$3`=system). |
| `install.sh` | Installateur automatisé macOS/Linux. |
| `install.ps1` | Installateur automatisé Windows. |

## Liens liés

- `../pixelcade_sync/README_FR.md` - remplit `/pixelcade/<system>/*.png` sur votre carte SD afin que l'artwork puisse être affiché instantanément sans accès réseau sur l'ESP32.
- `ArcadeMatrix_RPi/core/ssh_installer.py` - le flux d'installation équivalent pour le projet RPi (même protocole de daemon, déclenché depuis son interface web au lieu d'un script autonome).
