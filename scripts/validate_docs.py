#!/usr/bin/env python3
import os
import sys
import re
import configparser

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD_CONF_PATH = os.path.join(ROOT_DIR, "release", "sdCard", "conf.ini")

REQUIRED_DOC_FILES = [
    "README.md",
    "README_FR.md",
    "README_ES.md",
    "docs/GETTING_STARTED.md",
    "docs/GETTING_STARTED_FR.md",
    "docs/GETTING_STARTED_ES.md",
    "docs/HARDWARE.md",
    "docs/HARDWARE_FR.md",
    "docs/HARDWARE_ES.md",
    "docs/WIRING.md",
    "docs/WIRING_FR.md",
    "docs/WIRING_ES.md",
    "docs/CONFIGURATION.md",
    "docs/CONFIGURATION_FR.md",
    "docs/CONFIGURATION_ES.md",
    "docs/DEVELOPER.md",
    "docs/DEVELOPER_FR.md",
    "docs/DEVELOPER_ES.md",
    "docs/ARCHITECTURE.md",
    "docs/ARCHITECTURE_FR.md",
    "docs/ARCHITECTURE_ES.md",
    "docs/ASSET_PIPELINE.md",
    "docs/ASSET_PIPELINE_FR.md",
    "docs/ASSET_PIPELINE_ES.md",
]

# Obsolete pattern checks
OBSOLETE_PATTERNS = [
    (re.compile(r'\bNTPSERVER\b'), "Obsolete config key 'NTPSERVER' found (use NTP_SERVER)"),
    (re.compile(r'\bFORMAT24H\b'), "Obsolete config key 'FORMAT24H' found (use FORMAT_24H)"),
    (re.compile(r'\bCOLOR_DEPTH\b'), "Obsolete config key 'COLOR_DEPTH' found (use PWM_BITS)"),
    (re.compile(r'\bSPRITE_COUNT\b'), "Obsolete config key 'SPRITE_COUNT' found"),
]

# Required section & key contract schema for SD conf.ini
EXPECTED_CONFIG_SCHEMA = {
    "WIFI": ["SSID", "PASSWORD", "HOSTNAME"],
    "MATRIX": ["WIDTH", "HEIGHT", "PANEL_TYPE", "CHAIN", "BRIGHTNESS_LIMIT", "PWM_BITS", "FORCE_SINGLE_BUFFER", "RGB_SEQUENCE", "LIMIT_REFRESH_RATE_HZ"],
    "MQTT": ["ENABLED", "BROKER", "PORT", "USER", "PASS", "DEVICE_NAME", "TOPIC_BATOCERA", "TOPIC_RECALBOX"],
    "TIME": ["NTP_SERVER", "TIMEZONE", "FORMAT_24H", "CLOCK_FONT", "CLOCK_SIZE", "CLOCK_THEME", "CLOCK_OFFSET_X", "CLOCK_OFFSET_Y", "CLOCK_COLOR_1", "CLOCK_COLOR_2"],
    "IDLE": ["ROTATION", "CLOCK_DURATION_SEC", "DATE_DURATION_SEC", "WEATHER_DURATION_SEC", "TEMP_DURATION_SEC", "DECIBEL_DURATION_SEC", "GIFS_COUNT", "FIGHTER_ENABLED", "FIGHTER_INTERVAL_SEC"],
    "ENVIRONMENT": ["UNIT", "TEMP_OFFSET"],
    "AUDIO": ["VISUALIZER_ENABLED", "VISUALIZER_MODE", "MIC_GAIN", "DB_CALIBRATION"],
    "DATE": ["THEME", "BACKGROUND_SPRITE", "FORMAT", "DATE_FONT", "DATE_SIZE", "DATE_OFFSET_X", "DATE_OFFSET_Y", "DATE_COLOR_1", "DATE_COLOR_2"],
    "WEATHER": ["API_KEY", "CITY", "LANG", "WEATHER_OFFSET_X", "WEATHER_OFFSET_Y"],
    "STANDBY": ["NIGHT_MODE_ENABLED", "TURN_OFF_AT", "WAKE_UP_AT", "NIGHT_BRIGHTNESS"],
    "FONTS": ["CUSTOM_FONT_PATH"],
    "CRYPTO": ["ENABLED", "SYMBOLS", "DURATION_SEC", "CACHE_TTL_MIN", "CURRENCY"],
    "STOCK": ["ENABLED", "SYMBOLS", "DURATION_SEC", "CACHE_TTL_MIN"],
}

def check_doc_file(rel_path):
    full_path = os.path.join(ROOT_DIR, rel_path)
    if not os.path.exists(full_path):
        print(f"❌ Document file missing: {rel_path}")
        return False

    with open(full_path, "r", encoding="utf-8") as f:
        content = f.read()

    errors = 0
    for pattern, description in OBSOLETE_PATTERNS:
        if pattern.search(content):
            print(f"❌ {rel_path}: {description}")
            errors += 1

    if errors == 0:
        print(f"  ✓ {rel_path}")
        return True
    return False

def check_sd_conf_ini():
    if not os.path.exists(SD_CONF_PATH):
        print(f"❌ SD Card conf.ini file missing: {SD_CONF_PATH}")
        return False

    with open(SD_CONF_PATH, "r", encoding="utf-8") as f:
        content = f.read()

    errors = 0
    obsolete_sd_keys = [
        ("NTPSERVER=", "Obsolete key 'NTPSERVER=' found in SD conf.ini. Use 'NTP_SERVER=' instead."),
        ("FORMAT24H=", "Obsolete key 'FORMAT24H=' found in SD conf.ini. Use 'FORMAT_24H=' instead."),
        ("COLOR_DEPTH=", "Obsolete key 'COLOR_DEPTH=' found in SD conf.ini. Use 'PWM_BITS=' instead."),
        ("SPRITE_COUNT=", "Obsolete key 'SPRITE_COUNT=' found in SD conf.ini."),
    ]

    for key, msg in obsolete_sd_keys:
        if key in content:
            print(f"❌ release/sdCard/conf.ini: {msg}")
            errors += 1

    if re.search(r'\bROTATION=.*sprites.*\b', content, re.IGNORECASE):
        print("❌ release/sdCard/conf.ini: 'sprites' is listed in ROTATION sequence.")
        errors += 1

    # Check for duplicate TURN_OFF_AT under [STANDBY]
    standby_match = re.search(r'\[STANDBY\](.*?)(?=\n\[|\Z)', content, re.DOTALL)
    if standby_match:
        standby_block = standby_match.group(1)
        if standby_block.count("TURN_OFF_AT=") > 1:
            print("❌ release/sdCard/conf.ini: Duplicate 'TURN_OFF_AT=' found in [STANDBY]. Expected 'WAKE_UP_AT='.")
            errors += 1
        if "WAKE_UP_AT=" not in standby_block:
            print("❌ release/sdCard/conf.ini: Missing 'WAKE_UP_AT=' in [STANDBY].")
            errors += 1

    # Section & key schema validation
    ini_parser = configparser.ConfigParser(comment_prefixes=(';', '#'), inline_comment_prefixes=(';', ' #'))
    try:
        ini_parser.read(SD_CONF_PATH)
        for section, req_keys in EXPECTED_CONFIG_SCHEMA.items():
            if not ini_parser.has_section(section):
                print(f"❌ release/sdCard/conf.ini: Missing expected section [{section}].")
                errors += 1
                continue
            for k in req_keys:
                if not ini_parser.has_option(section, k):
                    print(f"❌ release/sdCard/conf.ini: Missing required key '{k}' in section [{section}].")
                    errors += 1
    except Exception as e:
        print(f"❌ release/sdCard/conf.ini parsing error: {e}")
        errors += 1

    if errors == 0:
        print("  ✓ release/sdCard/conf.ini structure, sections, and keys valid.")
        return True
    return False

def main():
    print("🔍 Validating Documentation files & SD conf.ini...")
    all_ok = True
    for doc in REQUIRED_DOC_FILES:
        if not check_doc_file(doc):
            all_ok = False

    if not check_sd_conf_ini():
        all_ok = False

    if all_ok:
        print("🎉 Documentation & SD Config validation PASSED.")
        sys.exit(0)
    else:
        print("💥 Documentation & SD Config validation FAILED.")
        sys.exit(1)

if __name__ == "__main__":
    main()
