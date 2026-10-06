#!/usr/bin/env bash
# ==============================================================================
# ArcadeMatrix - Run STRESS-06 Torture Test Suite
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VENV_DIR="$PROJECT_ROOT/.venv"

# 1. Setup virtual environment if absent
if [ ! -d "$VENV_DIR" ]; then
    echo "[ArcadeMatrix] Creating virtual environment in $VENV_DIR..."
    python3 -m venv "$VENV_DIR"
fi

# 2. Activate virtual environment
source "$VENV_DIR/bin/activate"

# 3. Install/upgrade dependencies
pip install -q --upgrade pip
pip install -q -r "$SCRIPT_DIR/requirements.txt"

# 4. Execute torture suite with passed arguments
exec python "$SCRIPT_DIR/torture_test.py" "$@"
