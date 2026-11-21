#!/usr/bin/env bash
# ==============================================================================
# ArcadeMatrix - Run gif2amanim Animation Compiler
# Works both from a full repository checkout and from standalone ArcadeMatrix-tools.zip
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ -f "$SCRIPT_DIR/requirements.txt" ]; then
    REQ_FILE="$SCRIPT_DIR/requirements.txt"
elif [ -f "$PROJECT_ROOT/scripts/requirements.txt" ]; then
    REQ_FILE="$PROJECT_ROOT/scripts/requirements.txt"
else
    echo "[ERROR] Missing requirements.txt in $SCRIPT_DIR" >&2
    exit 1
fi

if [ -d "$PROJECT_ROOT/.venv" ]; then
    VENV_DIR="$PROJECT_ROOT/.venv"
else
    VENV_DIR="$SCRIPT_DIR/venv"
fi

# 1. Setup virtual environment if absent (inherit system Pillow when preinstalled, e.g. python3-pil)
if [ ! -d "$VENV_DIR" ]; then
    echo "[ArcadeMatrix] Creating virtual environment in $VENV_DIR..."
    python3 -m venv --system-site-packages "$VENV_DIR"
fi

# 2. Activate virtual environment
source "$VENV_DIR/bin/activate"

# 3. Install dependencies only if Pillow is not already available in venv
if ! python -c "import PIL" >/dev/null 2>&1; then
    pip install -q --upgrade pip
    pip install -q -r "$REQ_FILE"
fi

# 4. Execute compiler with passed arguments
exec python "$SCRIPT_DIR/gif2amanim.py" "$@"
