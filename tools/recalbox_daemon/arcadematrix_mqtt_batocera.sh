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

send_mqtt() {
    _PAYLOAD="$1"
    # 1. Try mosquitto_pub if available in PATH
    if command -v mosquitto_pub >/dev/null 2>&1; then
        mosquitto_pub -h "$BROKER" -t "$TOPIC" -m "$_PAYLOAD" >> "$LOG_FILE" 2>&1 &
        return 0
    fi

    # 2. Check standard bin locations for mosquitto_pub
    for p in /usr/bin/mosquitto_pub /usr/local/bin/mosquitto_pub /bin/mosquitto_pub; do
        if [ -x "$p" ]; then
            "$p" -h "$BROKER" -t "$TOPIC" -m "$_PAYLOAD" >> "$LOG_FILE" 2>&1 &
            return 0
        fi
    done

    # 3. Fallback to native Python 3 raw socket publisher (zero external dependencies)
    PYTHON_BIN=""
    if command -v python3 >/dev/null 2>&1; then
        PYTHON_BIN="python3"
    elif command -v python >/dev/null 2>&1; then
        PYTHON_BIN="python"
    elif [ -x /usr/bin/python3 ]; then
        PYTHON_BIN="/usr/bin/python3"
    fi

    if [ -n "$PYTHON_BIN" ]; then
        "$PYTHON_BIN" -c "
import sys, socket
broker, port, topic, payload = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
def enc(l):
    r = bytearray()
    while True:
        b = l % 128
        l //= 128
        if l > 0: b |= 0x80
        r.append(b)
        if l == 0: break
    return bytes(r)
try:
    cid = b'batocera_es'
    c_body = b'\x00\x04MQTT\x04\x02\x00\x3c' + len(cid).to_bytes(2, 'big') + cid
    c_pkt = b'\x10' + enc(len(c_body)) + c_body
    t_b = topic.encode('utf-8')
    m_b = payload.encode('utf-8')
    p_body = len(t_b).to_bytes(2, 'big') + t_b + m_b
    p_pkt = b'\x30' + enc(len(p_body)) + p_body
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2.0)
    s.connect((broker, port))
    s.sendall(c_pkt)
    s.recv(4)
    s.sendall(p_pkt)
    s.sendall(b'\xe0\x00')
    s.close()
except Exception as e:
    print(f'MQTT Error: {e}', file=sys.stderr)
" "$BROKER" 1883 "$TOPIC" "$_PAYLOAD" >> "$LOG_FILE" 2>&1 &
        return 0
    fi

    echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] ERROR: Neither mosquitto_pub nor python3 found" >> "$LOG_FILE"
    return 1
}

clean_name() {
    echo "$1" | sed -E \
        -e 's/^[Aa]rcade [Mm]anufacturer //' \
        -e 's/^[Aa]rcade [Ss]ystem //' \
        -e 's/^[Aa]rcade [Gg]enre //' \
        -e 's/^[Aa]rcade [Cc]ollection //' \
        -e 's/^[Mm]anufacturer //' \
        -e 's/^[Ss]ystem //' \
        -e 's/^[Gg]enre //' \
        -e 's/^[Cc]ollection //' \
        -e 's/"//g' | sed -E 's/^[_-]//' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'
}

extract_system() {
    _sys="$1"
    _rom="$2"
    _base="$3"
    if [ -z "$_sys" ] || [ "$_sys" = "$_base" ] || [ "$_sys" = "$_rom" ] || echo "$_sys" | grep -qE '/|\.'; then
        if echo "$_rom" | grep -q '/roms/'; then
            echo "$_rom" | sed -E 's|.*/roms/([^/]+)/.*|\1|'
        elif [ -n "$_rom" ]; then
            basename "$(dirname "$_rom")"
        else
            echo "$_sys"
        fi
    else
        echo "$_sys"
    fi
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
        SYS_NAME=$(extract_system "$SYS_NAME" "$ROM_PATH" "$GAME_BASENAME")
        GAME_CLEAN=$(clean_name "$GAME_BASENAME")
        SYS_CLEAN=$(clean_name "$SYS_NAME")
        PAYLOAD="{\"status\": \"playing\", \"game\": \"$GAME_CLEAN\", \"system\": \"$SYS_CLEAN\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: gameStart | Rom: $ROM_PATH | Sys: $SYS_NAME | Sent: $PAYLOAD" >> "$LOG_FILE"
        send_mqtt "$PAYLOAD"
        ;;

    gameStop)
        # Batocera system runner: $1=system, $2=emulator, $3=core, $4=rom_path
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
        GAME_BASENAME=""
        if [ -n "$ROM_PATH" ]; then
            GAME_BASENAME=$(basename "$ROM_PATH" | sed 's/\.[^.]*$//')
        fi
        SYS_NAME=$(extract_system "$SYS_NAME" "$ROM_PATH" "$GAME_BASENAME")
        GAME_CLEAN=$(clean_name "$GAME_BASENAME")
        SYS_CLEAN=$(clean_name "$SYS_NAME")
        PAYLOAD="{\"status\": \"stopped\", \"game\": \"$GAME_CLEAN\", \"system\": \"$SYS_CLEAN\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: gameStop | Rom: $ROM_PATH | Sys: $SYS_NAME | Sent: $PAYLOAD" >> "$LOG_FILE"
        send_mqtt "$PAYLOAD"
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
        if [ -n "$ROM_PATH" ]; then
            GAME_BASENAME=$(basename "$ROM_PATH" | sed 's/\.[^.]*$//')
            GAME_CLEAN=$(clean_name "$GAME_BASENAME")
        elif [ -n "$TITLE" ]; then
            GAME_CLEAN=$(clean_name "$TITLE")
        else
            GAME_BASENAME=""
            GAME_CLEAN=""
        fi
        SYS_NAME=$(extract_system "$SYS_NAME" "$ROM_PATH" "$GAME_BASENAME")
        SYS_CLEAN=$(clean_name "$SYS_NAME")
        PAYLOAD="{\"status\": \"browsing\", \"game\": \"$GAME_CLEAN\", \"system\": \"$SYS_CLEAN\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: game-selected | Rom: $ROM_PATH | Sys: $SYS_NAME | Title: $TITLE | Sent: $PAYLOAD" >> "$LOG_FILE"
        send_mqtt "$PAYLOAD"
        ;;

    system-selected|systemSelected)
        # ES system-selected: $1=system
        SYS_CLEAN=$(clean_name "$1")
        PAYLOAD="{\"status\": \"browsing\", \"system\": \"$SYS_CLEAN\", \"type\": \"system\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: system-selected | Sys: $1 | Sent: $PAYLOAD" >> "$LOG_FILE"
        send_mqtt "$PAYLOAD"
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
        if [ -n "$ROM_PATH" ]; then
            GAME_BASENAME=$(basename "$ROM_PATH" | sed 's/\.[^.]*$//')
            GAME_CLEAN=$(clean_name "$GAME_BASENAME")
        elif [ -n "$TITLE" ]; then
            GAME_BASENAME=""
            GAME_CLEAN=$(clean_name "$TITLE")
        else
            GAME_BASENAME=""
            GAME_CLEAN=""
        fi
        SYS_NAME=$(extract_system "$SYS_NAME" "$ROM_PATH" "$GAME_BASENAME")
        SYS_CLEAN=$(clean_name "$SYS_NAME")
        PAYLOAD="{\"status\": \"playing\", \"game\": \"$GAME_CLEAN\", \"system\": \"$SYS_CLEAN\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: game-start | Rom: $ROM_PATH | Sys: $SYS_NAME | Title: $TITLE | Sent: $PAYLOAD" >> "$LOG_FILE"
        send_mqtt "$PAYLOAD"
        ;;

    game-end)
        # ES game-end: $1=system, $2=rom_path, $3=game_title
        SYS_NAME="$1"
        ROM_PATH="$2"
        if echo "$1" | grep -qE '/|\.'; then
            ROM_PATH="$1"
            SYS_NAME="$2"
        fi
        GAME_BASENAME=""
        if [ -n "$ROM_PATH" ]; then
            GAME_BASENAME=$(basename "$ROM_PATH" | sed 's/\.[^.]*$//')
        fi
        SYS_NAME=$(extract_system "$SYS_NAME" "$ROM_PATH" "$GAME_BASENAME")
        GAME_CLEAN=$(clean_name "$GAME_BASENAME")
        SYS_CLEAN=$(clean_name "$SYS_NAME")
        PAYLOAD="{\"status\": \"stopped\", \"game\": \"$GAME_CLEAN\", \"system\": \"$SYS_CLEAN\"}"
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Event: game-end | Rom: $ROM_PATH | Sys: $SYS_NAME | Sent: $PAYLOAD" >> "$LOG_FILE"
        send_mqtt "$PAYLOAD"
        ;;

    *)
        echo "$(date '+%Y-%m-%d %H:%M:%S') [arcadematrix] Unhandled Event: $EVENT | Args: $*" >> "$LOG_FILE"
        ;;
esac
