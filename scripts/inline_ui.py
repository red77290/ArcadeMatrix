import os
import re

base_dir = "../ArcadeMatrix_RPi/api/www/"
html_path = os.path.join(base_dir, "index.html")

with open(html_path, "r", encoding="utf-8") as f:
    html_content = f.read()

# Replace CSS
def css_replacer(match):
    css_file = match.group(1)
    css_path = os.path.join(base_dir, css_file)
    if os.path.exists(css_path):
        with open(css_path, "r", encoding="utf-8") as f:
            return "<style>\n" + f.read() + "\n</style>"
    return match.group(0)

html_content = re.sub(r'<link rel="stylesheet" href="([^"]+)">', css_replacer, html_content)

# Replace JS
def js_replacer(match):
    js_file = match.group(1)
    js_path = os.path.join(base_dir, js_file)
    if os.path.exists(js_path):
        with open(js_path, "r", encoding="utf-8") as f:
            return "<script>\n" + f.read() + "\n</script>"
    return match.group(0)

html_content = re.sub(r'<script src="([^"]+)"></script>', js_replacer, html_content)

# Remove Pi-specific blocks (Optional, but let's just write to data/index.html first)
with open("data/index.html", "w", encoding="utf-8") as f:
    f.write(html_content)

print("Inlined UI successfully!")
