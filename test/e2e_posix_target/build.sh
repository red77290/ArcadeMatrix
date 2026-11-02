#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_BIN="${1:-${SCRIPT_DIR}/arcadematrix_esp32_e2e}"
CXX_BIN="${CXX:-c++}"

"${CXX_BIN}" -std=c++17 -O2 -pthread \
  -I"${REPO_ROOT}/test/native/mock" \
  -I"${REPO_ROOT}/include" \
  -I"${REPO_ROOT}/src" \
  "${SCRIPT_DIR}/arcadematrix_esp32_e2e_main.cpp" \
  "${REPO_ROOT}/src/core/DisplayArbiter.cpp" \
  "${REPO_ROOT}/src/core/EngineRegistry.cpp" \
  "${REPO_ROOT}/src/core/drawing/CanvasBufferedSurface.cpp" \
  "${REPO_ROOT}/src/core/drawing/Hub75BulkEncoder.cpp" \
  "${REPO_ROOT}/src/core/drawing/DmdzStreamDecoder.cpp" \
  "${REPO_ROOT}/src/core/net/DmdzStreamClient.cpp" \
  "${REPO_ROOT}/src/core/net/ShowcaseDiscovery.cpp" \
  "${REPO_ROOT}/src/engines/showcase/ShowcaseStreamPlayer.cpp" \
  -o "${OUT_BIN}"

echo "[build] Built ArcadeMatrix ESP32 POSIX E2E target -> ${OUT_BIN}"
