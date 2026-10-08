#!/usr/bin/env python3
import re
import os

assets_path = os.path.join(os.path.dirname(__file__), "../src/engines/clocks/SonicAssets.h")
with open(assets_path, "r", encoding="utf-8") as f:
    content = f.read()

pattern = re.compile(r'static\s+const\s+uint16_t\s+(\w+)\[(\d+)\]\s*=\s*\{([^}]+)\};', re.MULTILINE)

def palettize(content):
    while True:
        m = pattern.search(content)
        if not m:
            break
        array_name = m.group(1)
        count = int(m.group(2))
        raw_vals = [x.strip() for x in m.group(3).split(",") if x.strip()]
        
        # Unique colors, ensuring 0x000E (MASK) is index 0
        unique_colors = []
        has_mask = False
        for v in raw_vals:
            if v == "0x000E":
                has_mask = True
            elif v not in unique_colors:
                unique_colors.append(v)
                
        palette = ["0x000E"] + unique_colors
        color_to_idx = {c: i for i, c in enumerate(palette)}
        pixels = [color_to_idx.get(v, 0) for v in raw_vals]
        
        pal_name = f"{array_name}_PAL"
        pix_name = f"{array_name}_PIXELS"
        
        pal_str = f"static const uint16_t {pal_name}[{len(palette)}] PROGMEM = {{\n    " + ", ".join(palette) + "\n};\n"
        
        pix_lines = []
        for i in range(0, len(pixels), 16):
            chunk = pixels[i:i+16]
            pix_lines.append("    " + ", ".join(f"0x{p:02X}" for p in chunk) + ",")
        pix_str = f"static const uint8_t {pix_name}[{len(pixels)}] PROGMEM = {{\n" + "\n".join(pix_lines) + "\n};"
        
        replacement = f"{pal_str}{pix_str}"
        content = content[:m.start()] + replacement + content[m.end():]
    return content

new_content = palettize(content)

# Update SONIC_RING_FRAMES:
# Replace:
# static const uint16_t* const SONIC_RING_FRAMES[4] = {
#     SONIC_RING_F0, SONIC_RING_F1, SONIC_RING_F2, SONIC_RING_F3
# };
ring_frames_repl = """static const uint16_t* const SONIC_RING_PALS[4] = {
    SONIC_RING_F0_PAL, SONIC_RING_F1_PAL, SONIC_RING_F2_PAL, SONIC_RING_F3_PAL
};
static const uint8_t* const SONIC_RING_PIXELS[4] = {
    SONIC_RING_F0_PIXELS, SONIC_RING_F1_PIXELS, SONIC_RING_F2_PIXELS, SONIC_RING_F3_PIXELS
};
"""
new_content = re.sub(r'static\s+const\s+uint16_t\*\s+const\s+SONIC_RING_FRAMES\[4\]\s*=\s*\{[^}]+\};', ring_frames_repl, new_content)

with open(assets_path, "w", encoding="utf-8") as f:
    f.write(new_content)

print("Successfully palettized SonicAssets.h")
