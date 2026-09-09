#!/bin/sh
# ArcadeMatrix Batocera MQTT event hook script
# Handles both Batocera system runner events (gameStart, gameStop) in /userdata/system/scripts/
# and EmulationStation UI events (game-selected, system-selected, game-start, game-end)
# in /userdata/system/configs/emulationstation/scripts/
#
# {{BROKER}} is substituted by the installer with the ArcadeMatrix IP address.

BROKER="{{BROKER}}"
TOPIC="system/playing/batocera"
LOG_FILE="/userdata/system/scripts/daemon.log"

clean_name() {
    echo "$1" | sed -E \
        -e 's/^[Aa]rcade [Mm]anufacturer //' \
        -e 's/^[Aa]rcade [Ss]ystem //' \
        -e 's/^[Aa]rcade [Gg]enre //' \
        -e 's/^[Aa]rcade [Cc]ollection //' \
        -e 's/^[Mm]anufacturer //' \
        -e 's/^[Ss]ystem //' \
        -e 's/^[Gg]enre //' \
        -e 's/^[Cc]ollection //' | sed -E 's/^[_-]//' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'
}

EVENT="$1"
shift

# Check for parent directory event name if invoked via direct symlink
if [ -z "$EVENT" ] || [ ! -z "${EVENT##game*}" -a ! -z "${EVENT##system*}" ]; then
    PARENT_DIR="$(basename "$(dirname "$0")")"
    case "$PARENT_DIR" in
        game-selected|system-selected|game-start|game-end)
            exec /userdata/system/scripts/arcadematrix_mqtt.sh "$PARENT_DIR" "$EVENT" "$@"
            ;;
    esac
fi

if ! command -v mosquitto_pub >/dev/null 2>&1; then
    echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] ERROR: mosquitto_pub not found in PATH" >> "$LOG_FILE"
    exit 1
fi

case "$EVENT" in
    gameStart)
        # Batocera system runner: $1=system, $2=emulator, $3=core, $4=rom_path (5th arg total)
        SYS_NAME="$1"
        if [ -n "$4" ]; then
            ROM_PATH="$4"
        elif [ -n "$2" ] && echo "$2" | grep -qE '/|\.'; then
            ROM_PATH="$2"
        elif [ -n "$1" ] && echo "$1" | grep -qE '/|\.'; then
            ROM_PATH="$1"
            SYS_NAME="$2"
        else
            ROM_PATH="$1"
        fi
        GAME_BASENAME=$(basename "$ROM_PATH" | sed 's/\.[^.]*$//')
        GAME_CLEAN=$(clean_name "$GAME_BASENAME")
        SYS_CLEAN=$(clean_name "$SYS_NAME")
        PAYLOAD="{\"status\": \"playing\", \"game\": \"$GAME_CLEAN\", \"system\": \"$SYS_CLEAN\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: gameStart | Rom: $ROM_PATH | Sys: $SYS_NAME | Sent: $PAYLOAD" >> "$LOG_FILE"
        mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "$PAYLOAD" >> "$LOG_FILE" 2>&1 &
        ;;

    gameStop)
        PAYLOAD="{\"status\": \"stopped\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: gameStop | Sent: $PAYLOAD" >> "$LOG_FILE"
        mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "$PAYLOAD" >> "$LOG_FILE" 2>&1 &
        ;;

    game-selected|gameSelected)
        # ES game-selected: $1=system, $2=rom_path, $3=game_title
        SYS_NAME="$1"
        ROM_PATH="$2"
        TITLE="$3"
        if echo "$1" | grep -qE '/|\.'; then
            ROM_PATH="$1"
            SYS_NAME="$2"
            TITLE="$3"
        fi
        if [ -n "$TITLE" ]; then
            GAME_CLEAN=$(clean_name "$TITLE")
        else
            GAME_BASENAME=$(basename "$ROM_PATH" | sed 's/\.[^.]*$//')
            GAME_CLEAN=$(clean_name "$GAME_BASENAME")
        fi
        SYS_CLEAN=$(clean_name "$SYS_NAME")
        PAYLOAD="{\"status\": \"browsing\", \"game\": \"$GAME_CLEAN\", \"system\": \"$SYS_CLEAN\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: game-selected | Rom: $ROM_PATH | Sys: $SYS_NAME | Title: $TITLE | Sent: $PAYLOAD" >> "$LOG_FILE"
        mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "$PAYLOAD" >> "$LOG_FILE" 2>&1 &
        ;;

    system-selected|systemSelected)
        # ES system-selected: $1=system
        SYS_CLEAN=$(clean_name "$1")
        PAYLOAD="{\"status\": \"browsing\", \"system\": \"$SYS_CLEAN\", \"type\": \"system\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: system-selected | Sys: $1 | Sent: $PAYLOAD" >> "$LOG_FILE"
        mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "$PAYLOAD" >> "$LOG_FILE" 2>&1 &
        ;;

    game-start)
        # ES game-start: $1=system, $2=rom_path, $3=game_title
        SYS_NAME="$1"
        ROM_PATH="$2"
        TITLE="$3"
        if echo "$1" | grep -qE '/|\.'; then
            ROM_PATH="$1"
            SYS_NAME="$2"
            TITLE="$3"
        fi
        if [ -n "$TITLE" ]; then
            GAME_CLEAN=$(clean_name "$TITLE")
        else
            GAME_BASENAME=$(basename "$ROM_PATH" | sed 's/\.[^.]*$//')
            GAME_CLEAN=$(clean_name "$GAME_BASENAME")
        fi
        SYS_CLEAN=$(clean_name "$SYS_NAME")
        PAYLOAD="{\"status\": \"playing\", \"game\": \"$GAME_CLEAN\", \"system\": \"$SYS_CLEAN\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: game-start | Rom: $ROM_PATH | Sys: $SYS_NAME | Title: $TITLE | Sent: $PAYLOAD" >> "$LOG_FILE"
        mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "$PAYLOAD" >> "$LOG_FILE" 2>&1 &
        ;;

    game-end)
        PAYLOAD="{\"status\": \"stopped\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: game-end | Sent: $PAYLOAD" >> "$LOG_FILE"
        mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "$PAYLOAD" >> "$LOG_FILE" 2>&1 &
        ;;

    *)
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Unhandled Event: $EVENT | Args: $*" >> "$LOG_FILE"
        ;;
esac
