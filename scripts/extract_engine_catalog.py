import os
import re
import glob
import json

CONFIG_TYPES = {
    "BOOLEAN": 0, "INTEGER": 1, "FLOAT": 2, "STRING": 3,
    "ENUM": 4, "COLOR": 5, "DURATION": 6, "LIST": 7, "FILE_ASSET": 8
}

def extract_engine_catalog(engines_dir="src/engines"):
    engines = []
    for path in sorted(glob.glob(os.path.join(engines_dir, "*.cpp"))):
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        
        m_desc = re.search(r"EngineDescriptor\s+\w+::getDescriptor\(\)\s*const\s*\{(.*?)\n\}", content, re.DOTALL)
        if not m_desc:
            continue
        
        desc_body = m_desc.group(1)
        m_meta = re.search(r'\.metadata\s*=\s*(?:EngineMetadata)?\s*[\{\(]\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"', desc_body)
        if not m_meta:
            continue
        
        eng_id = m_meta.group(1)
        eng_name = m_meta.group(2)
        eng_cat = m_meta.group(3)
        
        # Requirements
        req = {
            "needs_psram": bool(re.search(r'\.needsPsram\s*=\s*true', desc_body)),
            "needs_audio": bool(re.search(r'\.needsAudio(?:Input)?\s*=\s*true', desc_body)),
            "needs_temp_sensor": bool(re.search(r'\.needsTempSensor\s*=\s*true', desc_body)),
            "needs_gyroscope": bool(re.search(r'\.needsGyroscope\s*=\s*true', desc_body)),
            "needs_network": bool(re.search(r'\.needsNetwork\s*=\s*true', desc_body)),
            "needs_sd": bool(re.search(r'\.needsSd\s*=\s*true', desc_body)),
            "needs_tls": bool(re.search(r'\.needsTls\s*=\s*true', desc_body)),
        }
        m_fps = re.search(r'\.targetFps\s*=\s*(\d+)', desc_body)
        req["target_fps"] = int(m_fps.group(1)) if m_fps else 30
        
        # Capabilities
        cap = {
            "supports_128x32": not bool(re.search(r'\.supports_128x32\s*=\s*false', desc_body)),
            "supports_256x64": not bool(re.search(r'\.supports_256x64\s*=\s*false', desc_body)),
            "realtime": bool(re.search(r'\.realtime\s*=\s*true', desc_body)),
            "interruptible": not bool(re.search(r'\.interruptible\s*=\s*false', desc_body)),
            "selfPaced": bool(re.search(r'\.selfPaced\s*=\s*true', desc_body))
        }
        
        # Fields
        fields = []
        cf_matches = re.finditer(r'ConfigField\s*\((.*?)\)(?=\s*[,}\n])', desc_body, re.DOTALL)
        for cf in cf_matches:
            raw_args = cf.group(1).strip()
            args = []
            cur = []
            in_str = False
            esc = False
            for c in raw_args:
                if c == "\\" and in_str:
                    esc = not esc
                    cur.append(c)
                elif c == '"' and not esc:
                    in_str = not in_str
                    cur.append(c)
                elif c == ',' and not in_str:
                    args.append("".join(cur).strip())
                    cur = []
                else:
                    esc = False
                    cur.append(c)
            if cur:
                args.append("".join(cur).strip())
            
            if len(args) >= 3:
                f_id = args[0].strip('"')
                f_type_str = args[1].replace("ConfigType::", "").strip()
                f_type = CONFIG_TYPES.get(f_type_str, 3)
                f_label = args[2].strip('"')
                f_desc = args[3].strip('"') if len(args) > 3 else ""
                f_def = args[4].strip('"') if len(args) > 4 else ""
                f_req = args[5].lower() == "true" if len(args) > 5 else False
                f_min = args[6].strip('"') if len(args) > 6 else ""
                f_max = args[7].strip('"') if len(args) > 7 else ""
                f_step = args[8].strip('"') if len(args) > 8 else ""
                f_opts = args[9].strip('"') if len(args) > 9 else ""
                f_ep = args[10].strip('"') if len(args) > 10 else ""
                f_mult = args[11].lower() == "true" if len(args) > 11 else False
                f_vis = args[12].strip('"') if len(args) > 12 else ""
                
                field_obj = {
                    "id": f_id,
                    "field_type": f_type,
                    "label": f_label,
                    "description": f_desc,
                    "default_value": f_def,
                    "required": f_req
                }
                if f_min: field_obj["min_val"] = f_min
                if f_max: field_obj["max_val"] = f_max
                if f_step: field_obj["step"] = f_step
                if f_opts: field_obj["options"] = f_opts
                if f_ep: field_obj["options_endpoint"] = f_ep
                if f_mult: field_obj["multiple"] = True
                if f_vis: field_obj["visible_when"] = f_vis
                fields.append(field_obj)
                
        engines.append({
            "metadata": {
                "id": eng_id,
                "name": eng_name,
                "category": eng_cat,
                "version": "3.4.0-dev"
            },
            "capabilities": cap,
            "requirements": req,
            "available": True,
            "schema_url": f"/api/engines?id={eng_id}",
            "schema": fields
        })
        
    return engines

CANONICAL_THEMES = {
    "0": "Nintendo", "1": "Capcom", "2": "Taito", "3": "Sega",
    "4": "Cave", "5": "Konami", "6": "SNK", "7": "Technos",
    "8": "IGS", "9": "Hudson", "10": "Banpresto", "11": "Namco",
    "12": "Street Fighter (Ryu)", "13": "Super Mario", "14": "Metal Slug (Marco)",
    "15": "Mega Man", "16": "Space Invaders", "17": "Bubble Bobble (Bub)",
    "18": "Cyberpunk", "19": "Flip Clock", "20": "Custom Gradient",
    "21": "True Matrix", "22": "Pong Clock", "23": "Tetris Clock",
    "24": "Word Clock", "25": "Binary Clock", "26": "Pac-Man Clock",
    "27": "Versus Clock", "28": "Slot Machine Clock", "29": "Tetris Game Boy",
    "tetris": "Tetris Clock", "matrix": "Matrix Rain", "matrix_rain": "Matrix Rain",
    "versus": "Capcom Versus", "arcade": "Arcade", "pacman": "Pac-Man",
    "flip": "Flip Clock", "word": "Word Clock", "cyberpunk": "Cyberpunk",
    "pong": "Pong", "binary": "Binary", "slot_machine": "Slot Machine"
}

def extract_themes(webserver_path="src/api/WebServerAPI.cpp"):
    themes = dict(CANONICAL_THEMES)
    if os.path.exists(webserver_path):
        with open(webserver_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        m = re.search(r'static\s+const\s+ThemeItem\s+themes\[\]\s*=\s*\{(.*?)\};', content, re.DOTALL)
        if m:
            for item in re.finditer(r'\{\s*(\d+)\s*,\s*"([^"]+)"\s*\}', m.group(1)):
                themes[item.group(1)] = item.group(2)
    return themes

def extract_engine_meta_map(catalog):
    meta_map = {}
    for e in catalog:
        m = e['metadata']
        meta_map[m['id']] = {
            "name": m['name'],
            "category": m['category'],
            "version": m.get('version', '3.4.0-dev')
        }
    return meta_map

if __name__ == "__main__":
    catalog = extract_engine_catalog()
    themes = extract_themes()
    print(f"Extracted {len(catalog)} engines and {len(themes)} themes:")
    for e in catalog:
        print(f" - {e['metadata']['id']} ({e['metadata']['name']}): {len(e['schema'])} fields")

