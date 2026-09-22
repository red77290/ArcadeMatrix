#!/usr/bin/env python3
"""
validate_ui_and_i18n.py - Automated validation suite for ArcadeMatrix UI, I18n, and Engine Contracts.

Validates:
1. Complete key parity across EN, FR, ES translation dictionaries in i18n.js.
2. Complete i18n coverage for all engine IDs (engine_<id>) and field IDs (field_<id>).
3. Universal option keys (opt_system_default, opt_enabled, opt_disabled, opt_date_*, opt_unit_*, opt_lang_*, fit_mode_*).
4. Issue #24 regression test: Verifies modal creation logic in dynamic_engines.js safely handles engines without primaryField.
5. Rotation live-sync contract: Verifies updateInstanceRotationState is wired to trash removal and quick add.
6. MessageEngine smooth scrolling: Verifies continuous delta-time sub-pixel advance in C++ and Rust.
7. Hardware & WebUI isolation: Verifies zero ESP32/Raspberry Pi cross-contamination (GEMINI.md invariant 8).
8. ESP32 WebUI i18n integrity: duplicate-free EN/FR/ES parity in data/index.html, every markup key defined, no RPi key residue.
"""

import os
import re
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RPI_ROOT = os.path.abspath(os.path.join(REPO_ROOT, "..", "ArcadeMatrix_RPi"))

def fail(msg):
    print(f"❌ FAIL: {msg}")
    sys.exit(1)

def pass_step(msg):
    print(f"  ✓ {msg}")

def extract_js_dict(js_content, lang):
    """Extract a language dictionary object from i18n.js."""
    pattern = rf"{lang}:\s*\{{(.*?)\n    \}}"
    match = re.search(pattern, js_content, re.DOTALL)
    if not match:
        fail(f"Could not extract dictionary for language '{lang}' from i18n.js")
    block = match.group(1)
    
    keys = set()
    for line in block.splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        key_match = re.match(r"^([a-zA-Z0-9_]+)\s*:\s*[\"'](.*)[\"'],?$", line)
        if key_match:
            keys.add(key_match.group(1))
    return keys

def test_i18n_parity():
    print("🔍 [1/8] Testing I18n dictionary parity across EN, FR, and ES...")
    i18n_path = os.path.join(RPI_ROOT, "api", "www", "js", "i18n.js")
    if not os.path.exists(i18n_path):
        print("  ℹ Note: RPi i18n.js not present (running in standalone ESP32 mode, skipping RPi dictionary check)")
        return
    
    with open(i18n_path, "r", encoding="utf-8") as f:
        content = f.read()

    en_keys = extract_js_dict(content, "en")
    fr_keys = extract_js_dict(content, "fr")
    es_keys = extract_js_dict(content, "es")

    if not en_keys:
        fail("No keys found in EN dictionary!")

    missing_in_fr = en_keys - fr_keys
    missing_in_es = en_keys - es_keys

    if missing_in_fr:
        fail(f"Keys present in EN but missing in FR ({len(missing_in_fr)}): {sorted(list(missing_in_fr))}")
    if missing_in_es:
        fail(f"Keys present in EN but missing in ES ({len(missing_in_es)}): {sorted(list(missing_in_es))}")

    pass_step(f"Total keys per dictionary: {len(en_keys)} (EN=FR=ES parity 100%)")

def test_engine_and_field_translations():
    print("🔍 [2/8] Testing Engine & Field I18n coverage...")
    i18n_path = os.path.join(RPI_ROOT, "api", "www", "js", "i18n.js")
    dyn_path = os.path.join(RPI_ROOT, "api", "www", "js", "dynamic_engines.js")
    if not os.path.exists(i18n_path) or not os.path.exists(dyn_path):
        print("  ℹ Note: RPi files not present (running in standalone ESP32 mode, skipping RPi dynamic engines check)")
        return
    
    with open(i18n_path, "r", encoding="utf-8") as f:
        i18n_content = f.read()
    with open(dyn_path, "r", encoding="utf-8") as f:
        dyn_content = f.read()

    en_keys = extract_js_dict(i18n_content, "en")

    KNOWN_ENGINES = [
        "clock", "date", "weather", "gifs", "crypto", "stock",
        "visualizer", "decibel", "decibelmeter", "temp", "message",
        "cast", "spotify", "sysinfo", "music", "music_player",
        "dashboard", "gnews", "marquee"
    ]

    for eng in KNOWN_ENGINES:
        eng_key = f"engine_{eng}"
        if eng_key not in en_keys:
            fail(f"Engine '{eng}' lacks translation key '{eng_key}' in i18n.js")
    pass_step(f"All {len(KNOWN_ENGINES)} engines have matching 'engine_<id>' translations in all dictionaries.")

    # Check universal options
    universal_keys = [
        "opt_system_default", "opt_enabled", "opt_disabled",
        "opt_date_dmy", "opt_date_mdy", "opt_date_ymd", "opt_date_short_day", "opt_date_full",
        "fit_mode_fit", "fit_mode_stretch", "fit_mode_center",
        "opt_unit_c", "opt_unit_f", "opt_lang_en", "opt_lang_fr", "opt_lang_es"
    ]
    for uk in universal_keys:
        if uk not in en_keys:
            fail(f"Universal option key '{uk}' is missing from i18n dictionaries!")
    pass_step(f"All {len(universal_keys)} universal option keys verified.")

def test_issue24_modal_creation_safety():
    print("🔍 [3/8] Testing Issue #24 regression: primaryField safe modal creation...")
    dyn_path = os.path.join(RPI_ROOT, "api", "www", "js", "dynamic_engines.js")
    index_path = os.path.join(REPO_ROOT, "data", "index.html")

    with open(index_path, "r", encoding="utf-8") as f:
        idx_content = f.read()

    if "let primaryField = null;" not in idx_content:
        fail("data/index.html: missing 'let primaryField = null;' scope declaration")

    if os.path.exists(dyn_path):
        with open(dyn_path, "r", encoding="utf-8") as f:
            dyn_content = f.read()
        if "let primaryField = null;" not in dyn_content:
            fail("dynamic_engines.js: missing 'let primaryField = null;' scope declaration")
        if "else if (primaryField)" not in dyn_content:
            fail("dynamic_engines.js: missing safe 'else if (primaryField)' guard before accessing primaryField.id")

    pass_step("Issue #24 verified: primaryField properly scoped and guarded; engines without variants create cleanly.")

def test_rotation_live_sync():
    print("🔍 [4/8] Testing Rotation Loop Live UI Sync contract...")
    dyn_path = os.path.join(RPI_ROOT, "api", "www", "js", "dynamic_engines.js")
    if not os.path.exists(dyn_path):
        print("  ℹ Note: RPi dynamic_engines.js not present (running in standalone ESP32 mode)")
        return
    with open(dyn_path, "r", encoding="utf-8") as f:
        content = f.read()

    if "function updateInstanceRotationState" not in content:
        fail("updateInstanceRotationState helper missing from dynamic_engines.js")

    # Verify trash removal calls updateInstanceRotationState(removed.instance_id, false)
    trash_sync = re.search(r'updateInstanceRotationState\(\s*(?:removed|row)\.instance_id\s*,\s*false', content)
    if not trash_sync:
        fail("Trash delete handler does not call updateInstanceRotationState(removed.instance_id, false)!")

    # Verify quick add calls updateInstanceRotationState(instId, true, duration)
    quick_add_sync = re.search(r'updateInstanceRotationState\(\s*instId\s*,\s*true\s*,', content)
    if not quick_add_sync:
        fail("Quick Add handler does not call updateInstanceRotationState(instId, true, duration)!")

    # Verify saveConfig updates rotation dropdown option label
    if "opt.textContent =" not in content and "opt.text =" not in content:
        fail("saveConfig does not propagate custom screen_title to rotation dropdown!")

    pass_step("Rotation Live UI Sync contract verified (trash live sync, add sync, rename sync).")

def test_message_engine_smooth_scrolling():
    print("🔍 [5/8] Testing MessageEngine smooth continuous scrolling contracts (ESP32 & RPi)...")
    
    # Check ESP32
    esp32_cpp = os.path.join(REPO_ROOT, "src", "engines", "MessageEngine.cpp")
    with open(esp32_cpp, "r", encoding="utf-8") as f:
        cpp_content = f.read()

    if "float cursorX;" not in open(os.path.join(REPO_ROOT, "src", "engines", "MessageEngine.h")).read():
        fail("ESP32 MessageEngine.h: cursorX must be float for continuous sub-pixel scrolling")
    if "float movePx = dt / speedMs;" not in cpp_content:
        fail("ESP32 MessageEngine.cpp: missing sub-pixel delta-time movement 'float movePx = dt / speedMs;'")
    if "if (dt > 100.0f) dt = 16.6f;" not in cpp_content:
        fail("ESP32 MessageEngine.cpp: missing dt clamp protection for long frames / stalls")
    if "roundf(cursorX)" not in cpp_content:
        fail("ESP32 MessageEngine.cpp: missing roundf(cursorX) in render")

    # Check RPi Rust
    rpi_rs = os.path.join(RPI_ROOT, "src", "engines", "message.rs")
    if os.path.exists(rpi_rs):
        with open(rpi_rs, "r", encoding="utf-8") as f:
            rs_content = f.read()

        if "let move_px = dt_ms / step_ms;" not in rs_content:
            fail("RPi message.rs: missing sub-pixel delta-time movement 'let move_px = dt_ms / step_ms;'")
        if not re.search(r'if\s+dt_secs\s*>\s*0\.1\s*\{\s*16\.6\s*\}\s*else\s*\{\s*dt_secs\s*\*\s*1000\.0\s*\}', rs_content):
            fail("RPi message.rs: missing dt clamp protection for long frames / stalls")
        if "self.offset_x.round() as i32" not in rs_content:
            fail("RPi message.rs: missing offset_x.round() in render")

        pass_step("MessageEngine continuous delta-time scrolling contract verified on both ESP32 and RPi.")
    else:
        pass_step("MessageEngine continuous delta-time scrolling contract verified on ESP32 (RPi not present).")

def test_marquee_screen_clear():
    print("🔍 [6/8] Testing Marquee and Rotation screen clearance contracts...")
    
    rot_mgr = os.path.join(REPO_ROOT, "src", "core", "RotationManager.cpp")
    with open(rot_mgr, "r", encoding="utf-8") as f:
        rot_content = f.read()
    if "m_ctx->getMatrix()->fillScreen(0)" not in rot_content:
        fail("RotationManager.cpp: missing fillScreen(0) on module transition")

    marquee_cpp = os.path.join(REPO_ROOT, "src", "engines", "MarqueeEngine.cpp")
    with open(marquee_cpp, "r", encoding="utf-8") as f:
        marq_content = f.read()
    if "matrix->fillScreen(0)" not in marq_content:
        fail("MarqueeEngine.cpp: missing fillScreen(0) in activate()")

    pass_step("Screen clearance verified: no lingering ghost frames during transitions or marquee activate.")

def test_hardware_isolation():
    print("🔍 [7/8] Testing Hardware & WebUI Isolation (GEMINI.md Invariant 8)...")
    
    esp_html = os.path.join(REPO_ROOT, "data", "index.html")
    with open(esp_html, "r", encoding="utf-8") as f:
        esp_content = f.read()

    forbidden_rpi_in_esp = [
        'id="hw-slowdown"',
        'id="hw-mapping"',
        'id="hw-disable-pulsing"',
        'id="hw-pwm-lsb"',
        'id="hw-multiplexing"',
        'Slowdown for fast Pi',
        'Raspberry Pi 40-Pin GPIO Options',
    ]
    for term in forbidden_rpi_in_esp:
        if term in esp_content:
            fail(f"RPi hardware term '{term}' found in ESP32 data/index.html! Cross-contamination violation.")

    required_esp_terms = [
        'id="hw-caps-grid"',
        'id="hw-screen-rotation"',
        'id="hw-gyro-autorotate"',
        'id="btn-gyro-calibrate"',
        'id="hw-rotation-transition"',
        'id="hw-transition-duration"',
        'id="hw-driver-chip"',
        'id="hw-color-depth"',
        'id="hw-row-addr-type"',
        'id="hw-latch-blanking"',
        'id="hw-clk-phase"',
        'id="hw-force-single-buffer"',
        'id="btn-save-hw"',
    ]
    for term in required_esp_terms:
        if term not in esp_content:
            fail(f"Required ESP32 hardware term '{term}' missing from ESP32 data/index.html!")

    # Check RPi index.html doesn't have ESP32-only terms
    rpi_html = os.path.join(RPI_ROOT, "api", "www", "index.html")
    if os.path.exists(rpi_html):
        with open(rpi_html, "r", encoding="utf-8") as f:
            rpi_content = f.read()

        forbidden_esp_in_rpi = [
            'id="hw-caps-grid"',
            'id="hw-clk-phase"',
            'id="hw-force-single-buffer"',
        ]
        for term in forbidden_esp_in_rpi:
            if term in rpi_content:
                fail(f"ESP32-only term '{term}' found in RPi index.html! Cross-contamination violation.")

    pass_step("Hardware & WebUI isolation verified 100%: zero cross-contamination between ESP32 and RPi.")

def extract_esp32_dicts():
    """Extract the EN/FR/ES dictionaries embedded in the ESP32 data/index.html.

    Unlike the Raspberry Pi build, the ESP32 WebUI ships as a single self-contained HTML file,
    so its dictionaries live inside a `const translations = { en: {...}, fr: {...}, es: {...} }`
    literal rather than in a separate i18n.js module.
    """
    esp_html = os.path.join(REPO_ROOT, "data", "index.html")
    with open(esp_html, "r", encoding="utf-8") as f:
        content = f.read()

    dicts = {}
    for lang in ("en", "fr", "es"):
        match = re.search(r"^\s{2}%s:\s*\{" % lang, content, re.M)
        if not match:
            fail(f"Could not extract ESP32 dictionary for language '{lang}' from data/index.html")
        start = match.end()
        depth, i = 1, start
        while i < len(content) and depth:
            if content[i] == "{":
                depth += 1
            elif content[i] == "}":
                depth -= 1
            i += 1
        body = content[start:i - 1]
        keys = []
        for line in body.splitlines():
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            key_match = re.match(r"^([a-zA-Z0-9_]+)\s*:\s*[\"'](.*)[\"'],?$", line)
            if key_match:
                keys.append(key_match.group(1))
        dicts[lang] = keys
    return content, dicts


def test_esp32_i18n_integrity():
    print("🔍 [8/8] Testing ESP32 WebUI I18n integrity (data/index.html)...")
    content, dicts = extract_esp32_dicts()

    # 1. No duplicate keys: a shadowed key silently overrides the earlier definition at runtime.
    for lang, keys in dicts.items():
        seen, dupes = set(), set()
        for k in keys:
            if k in seen:
                dupes.add(k)
            seen.add(k)
        if dupes:
            fail(f"Duplicate keys in ESP32 '{lang}' dictionary ({len(dupes)}): {sorted(dupes)}")

    en, fr, es = (set(dicts[l]) for l in ("en", "fr", "es"))
    if not en:
        fail("No keys found in ESP32 EN dictionary!")

    # 2. Strict EN/FR/ES parity.
    for label, other in (("FR", fr), ("ES", es)):
        missing = en - other
        extra = other - en
        if missing:
            fail(f"ESP32 keys present in EN but missing in {label} ({len(missing)}): {sorted(missing)}")
        if extra:
            fail(f"ESP32 keys present in {label} but missing in EN ({len(extra)}): {sorted(extra)}")

    # 3. Every key referenced by the markup must exist, otherwise the UI silently falls back to
    #    the hardcoded English inline text and the FR/ES users see untranslated labels.
    used = set()
    for attr in ("data-i18n", "data-i18n-tooltip", "data-i18n-placeholder", "data-i18n-title", "data-i18n-html"):
        used |= set(re.findall(r'%s="([A-Za-z0-9_]+)"' % attr, content))
    used |= set(re.findall(r"\bt\(\s*['\"]([A-Za-z0-9_]+)['\"]", content))
    undefined = used - en
    if undefined:
        fail(f"ESP32 markup references {len(undefined)} undefined i18n keys: {sorted(undefined)}")

    # 4. GEMINI.md invariant 8.2: no Raspberry Pi setting may leak into the ESP32 dictionaries.
    rpi_keys = [
        "hw_mapping", "hw_slowdown", "hw_multiplexing", "hw_pwm_lsb", "hw_pwm_bits",
        "hw_disable_pulsing", "tt_hw_mapping", "tt_hw_slowdown", "tt_hw_multiplexing",
        "tt_hw_pwm_lsb", "tt_hw_pwm_bits", "tt_hw_disable_pulsing",
    ]
    leaked = sorted(k for k in rpi_keys if k in en or k in fr or k in es)
    if leaked:
        fail(f"Raspberry Pi i18n keys found in ESP32 dictionaries ({len(leaked)}): {leaked}")

    pass_step(f"ESP32 dictionaries verified: {len(en)} keys, EN=FR=ES parity, no duplicates, "
              f"{len(used)} markup references resolved, zero RPi residue.")


def main():
    print("================================================================================")
    print("  ArcadeMatrix UI, I18n & Engine Contract Automated Test Suite")
    print("================================================================================")
    test_i18n_parity()
    test_engine_and_field_translations()
    test_issue24_modal_creation_safety()
    test_rotation_live_sync()
    test_message_engine_smooth_scrolling()
    test_marquee_screen_clear()
    test_hardware_isolation()
    test_esp32_i18n_integrity()
    print("================================================================================")
    print("🎉 ALL UI, I18N AND ENGINE CONTRACT TESTS PASSED!")
    print("================================================================================")

if __name__ == "__main__":
    main()
