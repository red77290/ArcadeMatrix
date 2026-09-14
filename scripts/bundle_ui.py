#!/usr/bin/env python3
"""
bundle_ui.py - Safe Asset Synchronizer for ArcadeMatrix ESP32 WebUI.

CRITICAL ARCHITECTURAL INVARIANT (GEMINI.md Section 8):
data/index.html is the SOLE MASTER for the ESP32 WebUI.
It is STRICTLY PROHIBITED to overwrite data/index.html with ../ArcadeMatrix_RPi/api/www/index.html.
Raspberry Pi hardware settings (slowdown, mapping, disable_pulsing, pwm_lsb, multiplexing)
must NEVER be injected into ESP32 WebUI.
"""

import os
import sys

print("🔒 Safe UI Bundler: Verifying ESP32 WebUI isolation...")

esp_index = "data/index.html"
if not os.path.exists(esp_index):
    print(f"❌ Error: {esp_index} not found.")
    sys.exit(1)

with open(esp_index, "r", encoding="utf-8") as f:
    content = f.read()

# Verify zero contamination
FORBIDDEN = [
    "hw-slowdown", "Slowdown GPIO",
    "hw-mapping", "HAT / Mapping",
    "hw-disable-pulsing", "Disable Hardware Pulsing",
    "hw-pwm-lsb", "PWM LSB",
    "hw-multiplexing"
]
for item in FORBIDDEN:
    if item in content:
        print(f"❌ Isolation breach: '{item}' found in {esp_index}!")
        sys.exit(1)

print("✅ ESP32 WebUI is clean and isolated from Raspberry Pi hardware controls.")
