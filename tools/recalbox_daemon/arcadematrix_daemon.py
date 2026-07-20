"""
ArcadeMatrix Recalbox event daemon (lightweight, zero image processing).

Runs directly ON the Recalbox device (not the ESP32/RPi). Watches EmulationStation's
/tmp/es_state.inf state file and publishes a compact JSON event over MQTT whenever the
selected/playing game changes, in the exact format both ArcadeMatrix projects expect:

    {"status": "browsing"|"playing"|"stopped", "game": "<rom basename, no extension>", "system": "<SystemId>"}

- ArcadeMatrix_RPi subscribes to this on `recalbox/system/playing` (see main.py) and looks up/
  downloads the matching Pixelcade marquee image live.
- ArcadeMatrix (ESP32) subscribes to the same topic (see src/engines/RetroFrontendListener.cpp)
  and looks up a pre-synced Pixelcade image on its own SD card (see tools/pixelcade_sync/) - no
  network access needed on the ESP32 at all.

This file is a *template* - {{BROKER}} is substituted by install.sh/install.ps1 with the actual
IP address of your ArcadeMatrix device (ESP32 or Raspberry Pi) before being uploaded. If you are
installing manually over SSH instead of using the installer scripts, replace {{BROKER}} yourself
before copying this file to the device.
"""
import subprocess
import time
import os

BROKER = "{{BROKER}}"
TOPIC = "recalbox/system/playing"


def parse_statefile():
    game, system, state = None, None, "browsing"
    try:
        with open("/tmp/es_state.inf", "r") as f:
            for line in f:
                if line.startswith("GamePath="):
                    game = line.split("=", 1)[1].strip()
                elif line.startswith("SystemId="):
                    system = line.split("=", 1)[1].strip()
                elif line.startswith("State="):
                    state = line.split("=", 1)[1].strip()
    except Exception:
        pass
    return game, system, state


def main():
    import socket
    import sys
    # Single instance lock: guarantee only one daemon runs at a time, even if EmulationStation
    # (re)starts this launcher script multiple times in a row.
    lock_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        lock_socket.bind(("127.0.0.1", 49132))
    except socket.error:
        print("Another daemon is already running, exiting...")
        sys.exit(1)

    print("ArcadeMatrix daemon started (lightweight)!", flush=True)
    time.sleep(5)
    last_game = None
    last_sent = None
    pending_since = 0

    while True:
        try:
            rom_path, system, state = parse_statefile()
            if not rom_path:
                time.sleep(0.1)
                continue

            if rom_path != last_game:
                last_game = rom_path
                pending_since = time.time()

            # Debounce: wait 150ms of "hovering" before sending, to avoid spamming MQTT while the
            # user is scrolling quickly through the game list.
            elapsed = time.time() - pending_since
            if elapsed >= 0.15 and rom_path != last_sent:
                last_sent = rom_path
                gbase = os.path.splitext(os.path.basename(rom_path))[0]

                msg = '{"status": "' + state + '", "game": "' + gbase + '", "system": "' + str(system) + '"}'
                try:
                    subprocess.run(["mosquitto_pub", "-h", BROKER, "-t", TOPIC, "-m", msg], timeout=2, check=False)
                except subprocess.TimeoutExpired:
                    pass
        except Exception as e:
            print("Error: " + str(e), flush=True)

        time.sleep(0.1)


if __name__ == "__main__":
    main()
