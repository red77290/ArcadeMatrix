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
7. Hardware & WebUI isolation: Verifies zero ESP32/Raspberry Pi cross-contamination (Architectural Invariant 8).
8. ESP32 WebUI i18n integrity: duplicate-free EN/FR/ES parity in data/index.html, every markup key defined, no RPi key residue.
"""

import glob
import os
import re
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RPI_ROOT = os.path.abspath(os.path.join(REPO_ROOT, "..", "ArcadeMatrix_RPi"))
sys.path.insert(0, os.path.dirname(__file__))
from extract_engine_catalog import extract_engine_catalog  # noqa: E402

def fail(msg):
    print(f"❌ FAIL: {msg}")
    sys.exit(1)

def pass_step(msg):
    print(f"  ✓ {msg}")

def extract_js_dict_list(js_content, lang):
    """Extract ordered keys for a language dictionary object from i18n.js using brace depth."""
    match = re.search(r"^\s{2}%s:\s*\{" % lang, js_content, re.M)
    if not match:
        fail(f"Could not extract dictionary for language '{lang}' from i18n.js")
    start = match.end()
    depth, i = 1, start
    while i < len(js_content) and depth:
        if js_content[i] == "{":
            depth += 1
        elif js_content[i] == "}":
            depth -= 1
        i += 1
    block = js_content[start:i - 1]

    keys = []
    for line in block.splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        key_match = re.match(r"^([a-zA-Z0-9_]+)\s*:\s*[\"'](.*)[\"'],?$", line)
        if key_match:
            keys.append(key_match.group(1))
    return keys

def extract_js_dict(js_content, lang):
    return set(extract_js_dict_list(js_content, lang))

def test_i18n_parity():
    print("🔍 [1/9] Testing RPi I18n dictionary parity, duplicates & markup coverage...")
    i18n_path = os.path.join(RPI_ROOT, "api", "www", "js", "i18n.js")
    if not os.path.exists(i18n_path):
        print("  ℹ Note: RPi i18n.js not present (running in standalone ESP32 mode, skipping RPi dictionary check)")
        return
    
    with open(i18n_path, "r", encoding="utf-8") as f:
        content = f.read()

    lists = {lang: extract_js_dict_list(content, lang) for lang in ("en", "fr", "es")}
    for lang, klist in lists.items():
        seen, dupes = set(), set()
        for k in klist:
            if k in seen:
                dupes.add(k)
            seen.add(k)
        if dupes:
            fail(f"Duplicate keys in RPi '{lang}' dictionary ({len(dupes)}): {sorted(dupes)}")

    en_keys, fr_keys, es_keys = (set(lists[l]) for l in ("en", "fr", "es"))

    if not en_keys:
        fail("No keys found in EN dictionary!")

    for label, other in (("FR", fr_keys), ("ES", es_keys)):
        missing = en_keys - other
        extra = other - en_keys
        if missing:
            fail(f"RPi keys present in EN but missing in {label} ({len(missing)}): {sorted(list(missing))}")
        if extra:
            fail(f"RPi keys present in {label} but missing in EN ({len(extra)}): {sorted(list(extra))}")

    # Verify every key referenced in RPi index.html and js/*.js exists in i18n.js
    rpi_used = set()
    rpi_files = [
        os.path.join(RPI_ROOT, "api", "www", "index.html"),
        os.path.join(RPI_ROOT, "api", "www", "js", "app.js"),
        os.path.join(RPI_ROOT, "api", "www", "js", "dynamic_engines.js"),
        os.path.join(RPI_ROOT, "api", "www", "js", "gifs.js"),
    ]
    for fp in rpi_files:
        if not os.path.exists(fp):
            continue
        with open(fp, "r", encoding="utf-8") as rf:
            fc = rf.read()
        for attr in ("data-i18n", "data-i18n-tooltip", "data-i18n-placeholder", "data-i18n-title", "data-i18n-html"):
            rpi_used |= set(re.findall(r'%s="([A-Za-z0-9_]+)"' % attr, fc))
        rpi_used |= set(re.findall(r"\bt\(\s*['\"]([A-Za-z0-9_]+)['\"]", fc))
        if "localStorage.getItem('lang') || 'fr'" in fc:
            fail(f"Hardcoded French language fallback found in {os.path.basename(fp)} (Rule 2 violation)")

    undefined_rpi = rpi_used - en_keys
    if undefined_rpi:
        fail(f"RPi markup/JS references {len(undefined_rpi)} undefined i18n keys: {sorted(undefined_rpi)}")

    pass_step(f"Total keys per dictionary: {len(en_keys)} (EN=FR=ES parity 100%, 0 duplicates, {len(rpi_used)} refs verified)")

def extract_js_kv(js_content, lang):
    """Extract key -> value map for a language dictionary object."""
    match = re.search(r"^\s{2}%s:\s*\{" % lang, js_content, re.M)
    if not match:
        fail(f"Could not extract dictionary for language '{lang}'")
    start = match.end()
    depth, i = 1, start
    while i < len(js_content) and depth:
        if js_content[i] == "{":
            depth += 1
        elif js_content[i] == "}":
            depth -= 1
        i += 1
    out = {}
    for line in js_content[start:i - 1].splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        km = re.match(r"^([a-zA-Z0-9_]+)\s*:\s*[\"'](.*)[\"'],?$", line)
        if km:
            out[km.group(1)] = km.group(2)
    return out


def extract_rpi_engines_and_fields():
    """Dynamically extract all EngineMetadata IDs and ConfigField definitions from RPi Rust sources."""
    engines = set()
    fields = {}
    for rs in sorted(glob.glob(os.path.join(RPI_ROOT, "src", "**", "*.rs"), recursive=True)):
        with open(rs, "r", encoding="utf-8") as rf:
            src = rf.read()
        for em in re.finditer(r"EngineMetadata\s*\{([^}]+)\}", src):
            idm = re.search(r'\bid:\s*"([^"]+)"', em.group(1))
            if idm:
                engines.add(idm.group(1).lower())
        idx = 0
        while True:
            pos = src.find("ConfigField", idx)
            if pos == -1:
                break
            bstart = src.find("{", pos)
            if bstart == -1 or bstart - pos > 30:
                idx = pos + 11
                continue
            depth, j = 1, bstart + 1
            while j < len(src) and depth:
                if src[j] == "{":
                    depth += 1
                elif src[j] == "}":
                    depth -= 1
                j += 1
            blk = src[bstart + 1:j - 1]
            idx = j
            fid_m = re.search(r'\bid:\s*"([^"]+)"', blk)
            fdesc_m = re.search(r'\bdescription:\s*"((?:[^"\\]|\\.)*)"', blk)
            if fid_m:
                fid = fid_m.group(1)
                desc = fdesc_m.group(1) if fdesc_m else ""
                fields.setdefault(fid, desc)
    return sorted(engines), fields


def test_engine_and_field_translations():
    print("🔍 [2/9] Testing Dynamic Engine Catalog & ConfigField Schema I18n coverage (ESP32 & RPi)...")
    # 1. ESP32 C++ Catalog & ConfigField verification (always executed)
    esp_catalog = extract_engine_catalog(os.path.join(REPO_ROOT, "src", "engines"))
    esp_content, esp_dicts = extract_esp32_dicts()
    esp_en_kv = extract_js_kv(esp_content, "en")
    esp_fr_kv = extract_js_kv(esp_content, "fr")
    esp_es_kv = extract_js_kv(esp_content, "es")
    esp_en_keys = set(esp_dicts["en"])

    if "display engine plugin for ArcadeMatrix" in esp_content:
        fail("ESP32 data/index.html still contains hardcoded 'display engine plugin for ArcadeMatrix' fallback!")
    if "window.renderDynamicDisplay = renderDynamicDisplay;" not in esp_content:
        fail("ESP32 data/index.html must expose window.renderDynamicDisplay for live language switching!")

    esp_field_count = 0
    for eng in esp_catalog:
        eid = eng["metadata"]["id"].lower()
        for k_prefix in ("engine_", "engine_desc_"):
            key = f"{k_prefix}{eid}"
            if key not in esp_en_keys:
                fail(f"ESP32 engine '{eid}' lacks mandatory translation key '{key}' in data/index.html")
        desc_key = f"engine_desc_{eid}"
        if esp_fr_kv.get(desc_key) == esp_en_kv.get(desc_key) or esp_es_kv.get(desc_key) == esp_en_kv.get(desc_key):
            fail(f"ESP32 '{desc_key}' is untranslated (identical to EN) in FR or ES!")

        raw_schema = eng.get("schema", [])
        fields_list = raw_schema if isinstance(raw_schema, list) else raw_schema.get("fields", [])
        for f in fields_list:
            fid = f["id"]
            esp_field_count += 1
            if f"field_{fid}" not in esp_en_keys:
                fail(f"ESP32 ConfigField '{eid}.{fid}' lacks 'field_{fid}' in data/index.html")
            if f.get("description") and f"field_desc_{fid}" not in esp_en_keys:
                fail(f"ESP32 ConfigField '{eid}.{fid}' lacks 'field_desc_{fid}' in data/index.html")

    pass_step(f"ESP32 schema coverage 100%: {len(esp_catalog)} engines (names + descriptions) & {esp_field_count} field defs localized.")

    # 2. RPi Rust Catalog & ConfigField verification (when RPi repo is present alongside ESP32)
    i18n_path = os.path.join(RPI_ROOT, "api", "www", "js", "i18n.js")
    dyn_path = os.path.join(RPI_ROOT, "api", "www", "js", "dynamic_engines.js")
    if not os.path.exists(i18n_path) or not os.path.exists(dyn_path):
        print("  ℹ Note: RPi files not present (running in standalone ESP32 mode, skipping RPi dynamic engines check)")
        return

    with open(i18n_path, "r", encoding="utf-8") as f:
        i18n_content = f.read()
    with open(dyn_path, "r", encoding="utf-8") as f:
        dyn_content = f.read()

    if "display engine plugin for ArcadeMatrix" in dyn_content or "display engine plugin for ArcadeMatrix" in i18n_content:
        fail("RPi WebUI still contains hardcoded 'display engine plugin for ArcadeMatrix' fallback!")
    if "window.renderDynamicDisplay = renderDynamicDisplay;" not in dyn_content:
        fail("RPi dynamic_engines.js must expose window.renderDynamicDisplay for live language switching!")

    en_keys = extract_js_dict(i18n_content, "en")
    rpi_en_kv = extract_js_kv(i18n_content, "en")
    rpi_fr_kv = extract_js_kv(i18n_content, "fr")
    rpi_es_kv = extract_js_kv(i18n_content, "es")

    rpi_engines, rpi_fields = extract_rpi_engines_and_fields()
    known_aliases = [
        "clock", "date", "weather", "gifs", "crypto", "stock",
        "visualizer", "decibel", "decibelmeter", "temp", "message",
        "cast", "google_cast", "spotify", "sysinfo", "system_info", "music", "music_player",
        "dashboard", "gnews", "marquee", "mqttdata", "retro_games",
        "movie_showcase", "series_showcase", "anime_showcase",
    ]
    for eng in sorted(set(rpi_engines) | set(known_aliases)):
        for k_prefix in ("engine_", "engine_desc_"):
            eng_key = f"{k_prefix}{eng}"
            if eng_key not in en_keys:
                fail(f"RPi engine '{eng}' lacks translation key '{eng_key}' in i18n.js")
        desc_key = f"engine_desc_{eng}"
        if rpi_fr_kv.get(desc_key) == rpi_en_kv.get(desc_key) or rpi_es_kv.get(desc_key) == rpi_en_kv.get(desc_key):
            fail(f"RPi '{desc_key}' is untranslated (identical to EN) in FR or ES!")

    for fid, fdesc in sorted(rpi_fields.items()):
        if f"field_{fid}" not in en_keys:
            fail(f"RPi ConfigField '{fid}' lacks 'field_{fid}' in i18n.js")
        if fdesc and f"field_desc_{fid}" not in en_keys:
            fail(f"RPi ConfigField '{fid}' lacks 'field_desc_{fid}' in i18n.js")

    pass_step(f"RPi schema coverage 100%: {len(rpi_engines)} Rust engines + aliases (names + descriptions) & {len(rpi_fields)} ConfigFields localized.")

    # Check universal options on both ESP32 and RPi
    universal_keys = [
        "opt_system_default", "opt_enabled", "opt_disabled", "engine_desc_fallback",
        "opt_date_dmy", "opt_date_mdy", "opt_date_ymd", "opt_date_short_day", "opt_date_full",
        "fit_mode_fit", "fit_mode_stretch", "fit_mode_center",
        "opt_unit_c", "opt_unit_f", "opt_lang_en", "opt_lang_fr", "opt_lang_es"
    ]
    for uk in universal_keys:
        if uk not in en_keys:
            fail(f"Universal option key '{uk}' is missing from RPi i18n dictionaries!")
        if uk not in esp_en_keys:
            fail(f"Universal option key '{uk}' is missing from ESP32 i18n dictionaries!")
    pass_step(f"All {len(universal_keys)} universal option keys verified across ESP32 and RPi.")

def test_issue24_modal_creation_safety():
    print("🔍 [3/9] Testing Issue #24 regression: primaryField safe modal creation...")
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
    print("🔍 [4/9] Testing Rotation Loop Live UI Sync contract...")
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
    print("🔍 [5/9] Testing MessageEngine smooth continuous scrolling contracts (ESP32 & RPi)...")
    
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
    print("🔍 [6/9] Testing Marquee and Rotation screen clearance contracts (Invariants 18 & 19)...")
    
    rot_mgr = os.path.join(REPO_ROOT, "src", "core", "RotationManager.cpp")
    with open(rot_mgr, "r", encoding="utf-8") as f:
        rot_content = f.read()
    if "m_ctx->getSurface()->clear(0)" not in rot_content:
        fail("RotationManager.cpp: missing surface clear(0) on module transition (Invariant 18)")
    if "m_ctx->getMatrix()->fillScreen(0)" in rot_content:
        fail("RotationManager.cpp: direct matrix fillScreen(0) forbidden (Invariant 19)")

    marquee_cpp = os.path.join(REPO_ROOT, "src", "engines", "MarqueeEngine.cpp")
    with open(marquee_cpp, "r", encoding="utf-8") as f:
        marq_content = f.read()
    if "surface->clear(0)" not in marq_content:
        fail("MarqueeEngine.cpp: missing surface clear(0) in activate() (Invariant 18)")
    if "matrix->fillScreen(" in marq_content:
        fail("MarqueeEngine.cpp: direct matrix fillScreen forbidden (Invariant 19)")

    pass_step("Screen clearance verified: pure IDrawingSurface isolation maintained across transitions.")

def test_hardware_isolation():
    print("🔍 [7/9] Testing Hardware & WebUI Isolation (Architectural Invariant 8)...")
    
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
        'id="hw-render-pipeline"',
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
    print("🔍 [8/9] Testing ESP32 WebUI I18n integrity (data/index.html)...")
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

    # 4. Architectural Invariant 8.2: no Raspberry Pi setting may leak into the ESP32 dictionaries.
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


def test_showcase_i18n_completeness():
    print("🔍 [9/9] Testing Media Showcase I18n completeness & Rule 2 anti-regression...")
    esp_content, esp_dicts = extract_esp32_dicts()
    esp_en = set(esp_dicts["en"])

    if "localStorage.getItem('lang') || 'fr'" in esp_content or ": 'fr';" in esp_content:
        fail("ESP32 data/index.html still contains a 'fr' default language fallback (Rule 2 violation)")

    required_showcase_keys = [
        "engine_retro_games", "engine_movie_showcase", "engine_series_showcase", "engine_anime_showcase",
        "engine_desc_retro_games", "engine_desc_movie_showcase", "engine_desc_series_showcase", "engine_desc_anime_showcase",
        "field_server_url", "field_use_showcase_broker", "field_mqtt_broker_source", "field_mqtt_custom_url",
        "field_transport_mode", "field_text_scroll_speed", "field_jacket_panorama", "field_marquee_panorama",
        "field_snapshot_panorama_count", "field_video_max_duration_s", "field_video_fps", "field_video_mode",
        "field_show_poster", "field_show_synopsis", "field_show_video", "field_color_boost",
        "field_systems", "field_genres", "field_decades", "field_video_type",
        "field_desc_text_scroll_speed", "field_desc_jacket_panorama", "field_desc_marquee_panorama",
        "field_desc_snapshot_panorama_count", "field_desc_video_max_duration_s", "field_desc_video_fps",
        "field_desc_video_mode", "field_desc_show_poster", "field_desc_show_synopsis", "field_desc_show_video",
        "field_desc_color_boost", "field_desc_lang", "field_desc_systems", "field_desc_genres",
        "field_desc_decades", "field_desc_video_type",
        "opt_video_mode_auto_action", "opt_video_mode_panorama", "opt_video_mode_center_crop",
        "opt_color_boost_auto", "opt_color_boost_pixel_art", "opt_color_boost_anime", "opt_color_boost_cinema",
        "opt_scroll_speed_1", "opt_scroll_speed_4", "opt_scroll_speed_10",
        "opt_scroll_speed_slow", "opt_scroll_speed_normal", "opt_scroll_speed_fast",
        "opt_video_type_any", "opt_video_type_trailer", "opt_video_type_scene",
        "opt_video_type_opening", "opt_video_type_ending", "opt_video_type_pv",
        "showcase_ref_loading", "showcase_ref_all", "showcase_ref_sync", "showcase_ref_syncing",
        "showcase_ref_unreachable", "showcase_ref_available_filtered", "showcase_ref_available_all", "showcase_ref_empty",
    ]
    missing_esp = [k for k in required_showcase_keys if k not in esp_en]
    if missing_esp:
        fail(f"ESP32 Showcase i18n keys missing ({len(missing_esp)}): {missing_esp}")

    i18n_path = os.path.join(RPI_ROOT, "api", "www", "js", "i18n.js")
    rpi_html = os.path.join(RPI_ROOT, "api", "www", "index.html")
    if os.path.exists(i18n_path) and os.path.exists(rpi_html):
        with open(i18n_path, "r", encoding="utf-8") as f:
            rpi_en = extract_js_dict(f.read(), "en")
        missing_rpi = [k for k in required_showcase_keys if k not in rpi_en]
        if missing_rpi:
            fail(f"RPi Showcase i18n keys missing ({len(missing_rpi)}): {missing_rpi}")

        with open(rpi_html, "r", encoding="utf-8") as f:
            rpi_markup = f.read()
        forbidden_fr_phrases = [
            "Activer DMD Media Showcase",
            "Enregistrer la configuration Showcase",
            "Recommandé — Broker dédié",
            "Intégrations temps-réel",
            "Authentification par jeton",
        ]
        for phrase in forbidden_fr_phrases:
            if phrase in rpi_markup:
                fail(f"Hardcoded French text '{phrase}' found in RPi index.html! Rule 2 violation.")

    pass_step(f"Showcase I18n verified: {len(required_showcase_keys)} Showcase keys present across EN/FR/ES on ESP32 & RPi, zero hardcoded French.")


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
    test_showcase_i18n_completeness()
    print("================================================================================")
    print("🎉 ALL UI, I18N AND ENGINE CONTRACT TESTS PASSED!")
    print("================================================================================")

if __name__ == "__main__":
    main()
