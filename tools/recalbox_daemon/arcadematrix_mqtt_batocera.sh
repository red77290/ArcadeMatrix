#!/bin/sh
# ArcadeMatrix Batocera hook script - Batocera invokes this once per event (game start/select/stop)
# with $1=action $2=rom_path $3=system_name, unlike Recalbox's polling-daemon approach, so this is
# a plain one-shot script rather than a background process.
#
# This is a *template* - {{BROKER}} is substituted by install.sh/install.ps1 with your
# ArcadeMatrix device's IP address before being uploaded. If installing manually, replace it
# yourself first.

BROKER="{{BROKER}}"
TOPIC="recalbox/system/playing"
ACTION=$1
ROM_PATH=$2
SYSTEM_NAME=$3

if [ "$ACTION" = "gameStart" ] || [ "$ACTION" = "gameSelected" ]; then
    GAME_BASENAME=$(basename "$ROM_PATH" | sed 's/\.[^.]*$//')
    STATUS="playing"
    if [ "$ACTION" = "gameSelected" ]; then STATUS="browsing"; fi
    mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "{\"status\": \"$STATUS\", \"game\": \"$GAME_BASENAME\", \"system\": \"$SYSTEM_NAME\"}" &
elif [ "$ACTION" = "gameStop" ]; then
    mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "{\"status\": \"stopped\"}" &
fi
