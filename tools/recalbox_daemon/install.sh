#!/usr/bin/env bash
# ArcadeMatrix Recalbox/Batocera daemon installer - run this on your PC (macOS/Linux), NOT on the
# Recalbox/Batocera device itself. It connects over SSH, auto-detects whether the target is
# Recalbox or Batocera, uploads the right daemon/hook script (with your ArcadeMatrix device's IP
# baked in), and reboots the target so it starts sending game events over MQTT.
#
# This mirrors ArcadeMatrix_RPi's core/ssh_installer.py (used by its web UI's "Install" button),
# but works standalone from any PC, for ESP32 setups that have no such UI.
#
# Requirements: a standard OpenSSH client (ssh/scp), present by default on macOS and virtually all
# Linux distros. `sshpass` is optional but recommended (lets this script try Recalbox's/Batocera's
# well-known default passwords automatically); without it you'll be prompted for the password
# once per attempt.
set -euo pipefail

RECALBOX_PASS="recalboxroot"
BATOCERA_PASS="linux"
SSH_USER="root"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=============================================="
echo " ArcadeMatrix Recalbox/Batocera Daemon Installer"
echo "=============================================="
echo

read -rp "IP address of your Recalbox/Batocera device: " TARGET_IP
read -rp "IP address of your ArcadeMatrix device (ESP32 or Raspberry Pi): " BROKER_IP

if [ -z "$TARGET_IP" ] || [ -z "$BROKER_IP" ]; then
    echo "Both IP addresses are required. Aborting." >&2
    exit 1
fi

have_sshpass=0
if command -v sshpass >/dev/null 2>&1; then
    have_sshpass=1
else
    echo
    echo "NOTE: 'sshpass' is not installed - you'll be prompted for the SSH password manually"
    echo "for each connection attempt below (once for Recalbox, again for Batocera if the first"
    echo "attempt fails). Install sshpass (e.g. 'brew install hudochenkov/sshpass/sshpass' on"
    echo "macOS, 'apt install sshpass' on Debian/Ubuntu) to skip this."
    echo
fi

ssh_run() {
    local password="$1"; shift
    if [ "$have_sshpass" = "1" ]; then
        sshpass -p "$password" ssh "${SSH_OPTS[@]}" "${SSH_USER}@${TARGET_IP}" "$@"
    else
        ssh "${SSH_OPTS[@]}" "${SSH_USER}@${TARGET_IP}" "$@"
    fi
}

scp_run() {
    local password="$1" src="$2" dst="$3"
    if [ "$have_sshpass" = "1" ]; then
        sshpass -p "$password" scp "${SSH_OPTS[@]}" "$src" "${SSH_USER}@${TARGET_IP}:${dst}"
    else
        scp "${SSH_OPTS[@]}" "$src" "${SSH_USER}@${TARGET_IP}:${dst}"
    fi
}

detect_system() {
    local password="$1"
    if ssh_run "$password" "test -d /recalbox/share" 2>/dev/null; then
        echo "recalbox"
    elif ssh_run "$password" "test -d /userdata/system" 2>/dev/null; then
        echo "batocera"
    else
        echo "unknown"
    fi
}

echo "Trying Recalbox (password: $RECALBOX_PASS)..."
SYSTEM=$(detect_system "$RECALBOX_PASS" || true)
PASSWORD="$RECALBOX_PASS"

if [ "$SYSTEM" != "recalbox" ] && [ "$SYSTEM" != "batocera" ]; then
    echo "Not Recalbox, trying Batocera (password: $BATOCERA_PASS)..."
    PASSWORD="$BATOCERA_PASS"
    SYSTEM=$(detect_system "$BATOCERA_PASS" || true)
fi

if [ "$SYSTEM" != "recalbox" ] && [ "$SYSTEM" != "batocera" ]; then
    echo
    echo "ERROR: could not connect/authenticate to $TARGET_IP as either Recalbox or Batocera." >&2
    echo "Check the IP address, that SSH is enabled on the device, and that it's powered on." >&2
    exit 1
fi

echo "Detected: $SYSTEM"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

if [ "$SYSTEM" = "recalbox" ]; then
    TARGET_DIR="/recalbox/share/userscripts"
    sed "s/{{BROKER}}/$BROKER_IP/g" "$SCRIPT_DIR/arcadematrix_daemon.py" > "$TMP_DIR/arcadematrix_daemon.py"

    echo "Cleaning up any previous install..."
    ssh_run "$PASSWORD" "pkill -f arcadematrix_daemon.py || true; pkill -f arcadematrix_mqtt.sh || true; rm -f $TARGET_DIR/arcadematrix_mqtt.sh"
    ssh_run "$PASSWORD" "mkdir -p $TARGET_DIR"

    echo "Uploading daemon..."
    scp_run "$PASSWORD" "$TMP_DIR/arcadematrix_daemon.py" "/recalbox/share/arcadematrix_daemon.py"
    scp_run "$PASSWORD" "$SCRIPT_DIR/arcadematrix_launcher(permanent).sh" "$TARGET_DIR/arcadematrix_launcher(permanent).sh"
    ssh_run "$PASSWORD" "chmod +x '$TARGET_DIR/arcadematrix_launcher(permanent).sh'"
else
    TARGET_DIR="/userdata/system/scripts"
    sed "s/{{BROKER}}/$BROKER_IP/g" "$SCRIPT_DIR/arcadematrix_mqtt_batocera.sh" > "$TMP_DIR/arcadematrix_mqtt.sh"

    echo "Cleaning up any previous install..."
    ssh_run "$PASSWORD" "pkill -f arcadematrix_mqtt.sh || true"
    ssh_run "$PASSWORD" "mkdir -p $TARGET_DIR"

    echo "Uploading hook script..."
    scp_run "$PASSWORD" "$TMP_DIR/arcadematrix_mqtt.sh" "$TARGET_DIR/arcadematrix_mqtt.sh"
    ssh_run "$PASSWORD" "chmod +x $TARGET_DIR/arcadematrix_mqtt.sh"
fi

echo "Rebooting $TARGET_IP to apply changes..."
ssh_run "$PASSWORD" "sleep 1 && reboot" || true

echo
echo "=============================================="
echo " Done! $SYSTEM is rebooting."
echo " It will publish game events to MQTT broker $BROKER_IP:1883 on topic"
echo " recalbox/system/playing once it's back up."
echo "=============================================="
