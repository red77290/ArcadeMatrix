import os
import re

base_dir = "../ArcadeMatrix_RPi/api/www/"
html_path = os.path.join(base_dir, "index.html")

with open(html_path, "r", encoding="utf-8") as f:
    html_content = f.read()

# 1. Inline CSS
def css_replacer(match):
    css_file = match.group(1).lstrip("/") # Remove leading slash
    css_path = os.path.join(base_dir, css_file)
    if os.path.exists(css_path):
        with open(css_path, "r", encoding="utf-8") as f:
            return "<style>\n" + f.read() + "\n</style>"
    return match.group(0)

html_content = re.sub(r'<link rel="stylesheet" href="([^"]+)">', css_replacer, html_content)

# 2. Bundle JS
js_files = ["js/api.js", "js/i18n.js", "js/components/toast.js", "js/dynamic_engines.js", "js/app.js"]
bundled_js = ""
for js_file in js_files:
    path = os.path.join(base_dir, js_file)
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
            # Remove imports
            content = re.sub(r'import\s+.*?;?\n', '', content)
            # Remove exports but keep the declarations
            content = re.sub(r'export\s+', '', content)
            bundled_js += f"\n// --- {js_file} ---\n{content}\n"

# Replace the module script tag with our bundled JS
html_content = re.sub(r'<script type="module" src="/?js/app.js"></script>', lambda m: f'<script>{bundled_js}</script>', html_content)

# Hide non-ESP32 sections (Pi specific)
# Let's replace the whole list item for "Hardware Mapping", "Slowdown", "Audio Conflict", etc.
html_content = re.sub(r'<div class="setting-item">[^<]*<label[^>]*>Hardware Mapping[\s\S]*?</select>[^<]*</div>', '', html_content)
html_content = re.sub(r'<div class="setting-item">[^<]*<label[^>]*>Matrix Slowdown[\s\S]*?</select>[^<]*</div>', '', html_content)
html_content = re.sub(r'<div class="setting-item">[^<]*<label[^>]*>Audio PWM Conflict[\s\S]*?</div>[^<]*</div>', '', html_content)
html_content = re.sub(r'<div class="setting-item">[^<]*<label[^>]*>PWM LSB Nanoseconds[\s\S]*?</div>[^<]*</div>', '', html_content)

with open("data/index.html", "w", encoding="utf-8") as f:
    f.write(html_content)

print("Bundled UI generated at data/index.html")
