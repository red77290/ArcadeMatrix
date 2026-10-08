#!/usr/bin/env python3
"""
generate_engine_screens.py - High-fidelity hardware simulation screens for all ArcadeMatrix engines.
Renders 100% faithful 128x64 HUB75 LED matrix displays directly aligned with the C++ rendering code in src/engines/.
Applies realistic LED matrix diode shaders and hardware bezels.
"""

import os
import math
from PIL import Image, ImageDraw, ImageFont

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINES_ASSET_DIR = os.path.join(ROOT_DIR, "docs", "assets", "engines")
os.makedirs(ENGINES_ASSET_DIR, exist_ok=True)

# ----------------------------------------------------------------------------
# 5x7 Pixel Font
# ----------------------------------------------------------------------------
FONT_5X7 = {
    'A': [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    'B': ["#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "],
    'C': [" ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####"],
    'D': ["#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "],
    'E': ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    'F': ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "],
    'G': [" ####", "#    ", "#    ", "# ###", "#   #", "#   #", " ####"],
    'H': ["#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    'I': ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"],
    'J': ["  ###", "    #", "    #", "    #", "#   #", "#   #", " ### "],
    'K': ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    'L': ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    'M': ["#   #", "## ##", "# # #", "#   #", "#   #", "#   #", "#   #"],
    'N': ["#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"],
    'O': [" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    'P': ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "],
    'Q': [" ### ", "#   #", "#   #", "#   #", "# # #", "#  ##", " ####"],
    'R': ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    'S': [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    'T': ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    'U': ["#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    'V': ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    'W': ["#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"],
    'X': ["#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"],
    'Y': ["#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "],
    'Z': ["#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"],
    '0': [" ### ", "#  ##", "# # #", "# # #", "##  #", "#   #", " ### "],
    '1': ["  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    '2': [" ### ", "#   #", "    #", "  ## ", " #   ", "#    ", "#####"],
    '3': ["#### ", "    #", "    #", " ### ", "    #", "    #", "#### "],
    '4': ["   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "],
    '5': ["#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "],
    '6': ["  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "],
    '7': ["#####", "    #", "   # ", "  #  ", "  #  ", "  #  ", "  #  "],
    '8': [" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "],
    '9': [" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "],
    ':': [" ", "  #  ", "  #  ", " ", "  #  ", "  #  ", " "],
    '.': [" ", " ", " ", " ", " ", "  ## ", "  ## "],
    ',': [" ", " ", " ", " ", "  ## ", "   # ", "  #  "],
    '-': [" ", " ", " ", "#####", " ", " ", " "],
    '+': [" ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", " "],
    '/': ["    #", "   # ", "   # ", "  #  ", " #   ", " #   ", "#    "],
    '%': ["##  #", "## # ", "  #  ", " #   ", " # ##", "#  ##", "     "],
    '°': [" ##  ", "#  # ", " ##  ", "     ", "     ", "     ", "     "],
    '$': ["  #  ", " ####", "# #  ", " ### ", "  # #", "#### ", "  #  "],
    '[': [" ### ", " #   ", " #   ", " #   ", " #   ", " #   ", " ### "],
    ']': [" ### ", "   # ", "   # ", "   # ", "   # ", "   # ", " ### "],
    '(': ["  ## ", " #   ", " #   ", " #   ", " #   ", " #   ", "  ## "],
    ')': [" ##  ", "   # ", "   # ", "   # ", "   # ", "   # ", " ##  "],
    '*': ["     ", "# # #", " ### ", "#####", " ### ", "# # #", "     "],
    '#': [" # # ", "#####", " # # ", "#####", " # # ", " # # ", "     "],
    '▲': ["  #  ", " ### ", "#####", "     ", "     ", "     ", "     "],
    '▼': ["     ", "     ", "#####", " ### ", "  #  ", "     ", "     "],
    '➔': ["  #  ", "   # ", "#####", "   # ", "  #  ", "     ", "     "],
    '★': ["  #  ", " ### ", "#####", " ### ", "# # #", "     ", "     "],
    ' ': ["     ", "     ", "     ", "     ", "     ", "     ", "     "],
}

# ----------------------------------------------------------------------------
# 3x5 Pixel Font (Full A-Z & 0-9)
# ----------------------------------------------------------------------------
FONT_3X5 = {
    'A': ["###", "#.#", "###", "#.#", "#.#"],
    'B': ["##.", "#.#", "##.", "#.#", "##."],
    'C': ["###", "#..", "#..", "#..", "###"],
    'D': ["##.", "#.#", "#.#", "#.#", "##."],
    'E': ["###", "#..", "##.", "#..", "###"],
    'F': ["###", "#..", "##.", "#..", "#.."],
    'G': ["###", "#..", "#.#", "#.#", "###"],
    'H': ["#.#", "#.#", "###", "#.#", "#.#"],
    'I': ["###", ".#.", ".#.", ".#.", "###"],
    'J': ["..#", "..#", "..#", "#.#", "###"],
    'K': ["#.#", "##.", "#..", "##.", "#.#"],
    'L': ["#..", "#..", "#..", "#..", "###"],
    'M': ["###", "#.#", "#.#", "#.#", "#.#"],
    'N': ["###", "#.#", "#.#", "#.#", "#.#"],
    'O': ["###", "#.#", "#.#", "#.#", "###"],
    'P': ["##.", "#.#", "##.", "#..", "#.."],
    'Q': ["###", "#.#", "#.#", "###", "..#"],
    'R': ["##.", "#.#", "##.", "#.#", "#.#"],
    'S': [".##", "#..", "###", "..#", "##."],
    'T': ["###", ".#.", ".#.", ".#.", ".#."],
    'U': ["#.#", "#.#", "#.#", "#.#", "###"],
    'V': ["#.#", "#.#", "#.#", "#.#", ".#."],
    'W': ["#.#", "#.#", "#.#", "###", "#.#"],
    'X': ["#.#", "#.#", ".#.", "#.#", "#.#"],
    'Y': ["#.#", "#.#", ".#.", ".#.", ".#."],
    'Z': ["###", "..#", ".#.", "#..", "###"],
    '0': ["###", "#.#", "#.#", "#.#", "###"],
    '1': [".#.", "##.", ".#.", ".#.", "###"],
    '2': ["###", "..#", "###", "#..", "###"],
    '3': ["###", "..#", "###", "..#", "###"],
    '4': ["#.#", "#.#", "###", "..#", "..#"],
    '5': ["###", "#..", "###", "..#", "###"],
    '6': ["###", "#..", "###", "#.#", "###"],
    '7': ["###", "..#", ".#.", ".#.", ".#."],
    '8': ["###", "#.#", "###", "#.#", "###"],
    '9': ["###", "#.#", "###", "..#", "###"],
    ':': [".", "#", ".", "#", "."],
    ' ': ["...", "...", "...", "...", "..."],
    '-': ["...", "...", "###", "...", "..."],
    '.': ["...", "...", "...", "..#", "..#"],
    '%': ["#.#", "..#", ".#.", "#..", "#.#"],
    '°': ["##.", "##.", "...", "...", "..."],
    '/': ["..#", ".#.", ".#.", ".#.", "#.."],
    '$': [".#.", "###", "#..", ".#.", "###"],
    '[': ["##.", "#..", "#..", "#..", "##."],
    ']': [".##", "..#", "..#", "..#", ".##"],
    '+': [".", ".#.", "###", ".#.", "."],
    '▲': [".#.", "###", "...", "...", "..."],
    '▼': ["...", "...", "###", ".#.", "..."],
    '•': ["...", ".#.", ".#.", "...", "..."],
    '➔': ["...", ".#.", "###", ".#.", "..."],
}

def draw_text(canvas, text, x, y, col=(255, 255, 255, 255), font="5x7", scale=1):
    cur_x = x
    font_map = FONT_5X7 if font == "5x7" else FONT_3X5
    def_char = font_map.get(' ')
    char_w = 5 if font == "5x7" else 3

    for ch in text.upper():
        bitmap = font_map.get(ch, def_char)
        for r, row in enumerate(bitmap):
            for c, val in enumerate(row):
                if val == '#' or val == '▲' or val == '▼' or val == '★':
                    for sy in range(scale):
                        for sx in range(scale):
                            px = cur_x + c * scale + sx
                            py = y + r * scale + sy
                            if 0 <= px < canvas.width and 0 <= py < canvas.height:
                                canvas.putpixel((px, py), col)
        cur_x += (char_w + 1) * scale
    return cur_x

def draw_text_with_shadow(canvas, text, x, y, col=(255, 215, 0, 255), shadow_col=(0, 75, 175, 255), depth=1, font="5x7", scale=1):
    """Draws arcade-style extruded drop shadow or outline exactly matching DateEngine / ArcadeClock."""
    for dy in range(1, depth + 1):
        for dx in range(1, depth + 1):
            draw_text(canvas, text, x + dx, y + dy, col=shadow_col, font=font, scale=scale)
    draw_text(canvas, text, x, y, col=col, font=font, scale=scale)

def apply_led_matrix_shader(raw_img, led_pitch=4, led_radius=1):
    """Renders realistic HUB75 diodes with pitch-black PCB substrate and soft diode glow."""
    w, h = raw_img.size
    out_w, out_h = w * led_pitch, h * led_pitch
    led_img = Image.new('RGBA', (out_w, out_h), (8, 9, 12, 255))
    draw = ImageDraw.Draw(led_img)
    pixels = raw_img.load()

    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            cx = x * led_pitch + led_pitch // 2
            cy = y * led_pitch + led_pitch // 2

            # Unlit diode / background PCB substrate
            if a < 20 or (r < 15 and g < 15 and b < 20):
                draw.ellipse([cx - led_radius, cy - led_radius,
                              cx + led_radius, cy + led_radius], fill=(16, 18, 24, 255))
                continue

            max_c = max(r, g, b)
            # Subtle diode halo glow
            if max_c > 110:
                glow_a = int(55 * (max_c / 255.0))
                draw.ellipse([cx - led_radius - 1, cy - led_radius - 1,
                              cx + led_radius + 1, cy + led_radius + 1],
                             fill=(r // 2, g // 2, b // 2, glow_a))

            # Core emitted diode
            draw.ellipse([cx - led_radius, cy - led_radius,
                          cx + led_radius, cy + led_radius], fill=(r, g, b, 255))
    return led_img

def wrap_with_hardware_bezel(led_img, title="ARCADE MATRIX", badge="HUB75 DMA"):
    """
    Surrounds the simulated LED matrix with a sleek matte hardware chassis,
    corner mounting screws, and subtle technical labeling.
    """
    pad_x, pad_top, pad_bot = 16, 14, 22
    total_w = led_img.width + pad_x * 2
    total_h = led_img.height + pad_top + pad_bot

    frame = Image.new('RGBA', (total_w, total_h), (18, 20, 26, 255))
    df = ImageDraw.Draw(frame)

    # Outer bezel border & bevel
    df.rectangle([0, 0, total_w - 1, total_h - 1], outline=(36, 42, 54, 255), width=2)
    df.rectangle([2, 2, total_w - 3, total_h - 3], outline=(10, 12, 16, 255), width=1)

    # 4 Corner Hex Mounting Screws
    screw_col = (70, 78, 92, 255)
    screw_center = (45, 52, 64, 255)
    for sx, sy in [(8, 8), (total_w - 9, 8), (8, total_h - 9), (total_w - 9, total_h - 9)]:
        df.ellipse([sx - 3, sy - 3, sx + 3, sy + 3], fill=screw_col)
        df.line([sx - 2, sy, sx + 2, sy], fill=screw_center)
        df.line([sx, sy - 2, sx, sy + 2], fill=screw_center)

    # Screen cutout inner shadow
    screen_x, screen_y = pad_x, pad_top
    df.rectangle([screen_x - 1, screen_y - 1, screen_x + led_img.width, screen_y + led_img.height],
                 outline=(6, 8, 10, 255), width=1)

    # Paste the glowing LED matrix
    frame.paste(led_img, (screen_x, screen_y))

    # Bottom technical inscription
    try:
        font_sub = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 9)
    except:
        font_sub = ImageFont.load_default()

    df.text((screen_x + 4, total_h - 16), title, font=font_sub, fill=(110, 125, 150, 255))
    df.text((total_w - pad_x - 70, total_h - 16), badge, font=font_sub, fill=(80, 180, 240, 255))

    return frame

def create_base_canvas(w=128, h=64, bg=(8, 10, 14, 255)):
    img = Image.new('RGBA', (w, h), bg)
    return img, ImageDraw.Draw(img)

# ----------------------------------------------------------------------------
# Specific Engine Visual Simulation Renderers (100% C++ Source Code Fidelity)
# ----------------------------------------------------------------------------

# 1. DASHBOARD ENGINE (src/engines/DashboardEngine.cpp)
def render_dashboard():
    img, draw = create_base_canvas()
    # Analog Watch Dial on Left (cx=26, cy=32, r=22) - PixelClockWidget::renderAnalog
    cx, cy, r = 26, 32, 22
    draw.ellipse([cx-r, cy-r, cx+r, cy+r], outline=(50, 65, 85, 255), width=1)
    draw.ellipse([cx-r+2, cy-r+2, cx+r-2, cy+r-2], outline=(30, 40, 55, 255))
    # Ticks
    for deg in range(0, 360, 30):
        rad = math.radians(deg)
        x1 = cx + int((r - 2) * math.cos(rad))
        y1 = cy + int((r - 2) * math.sin(rad))
        x2 = cx + int((r - 4) * math.cos(rad))
        y2 = cy + int((r - 4) * math.sin(rad))
        col = (255, 215, 0, 255) if deg % 90 == 0 else (120, 140, 170, 255)
        draw.line([x1, y1, x2, y2], fill=col)
    # Hour hand (10:10)
    draw.line([cx, cy, cx - 8, cy - 8], fill=(240, 240, 240, 255), width=2)
    # Minute hand
    draw.line([cx, cy, cx + 12, cy - 10], fill=(220, 220, 220, 255), width=1)
    # Sweeping Red Second hand
    draw.line([cx, cy, cx + 14, cy + 10], fill=(255, 40, 40, 255), width=1)
    draw.ellipse([cx-2, cy-2, cx+2, cy+2], fill=(255, 40, 40, 255))

    # Top-Right: Climate & World Clock
    draw_text(img, "PARIS", 56, 8, col=(100, 220, 120, 255), font="3x5")
    draw_text(img, "15:42", 84, 7, col=(255, 255, 255, 255), font="5x7")
    draw_text(img, "21.8°C", 56, 18, col=(255, 140, 80, 255), font="3x5")
    draw_text(img, "46%RH", 88, 18, col=(60, 200, 255, 255), font="3x5")

    # Divider line
    draw.line([54, 28, 124, 28], fill=(35, 45, 60, 255))

    # Bottom-Right: Market Ticker
    draw_text(img, "BTC $84.3K", 56, 34, col=(255, 215, 0, 255), font="3x5")
    draw_text(img, "▲+5.4%", 98, 34, col=(80, 240, 120, 255), font="3x5")
    draw_text(img, "AAPL $228", 56, 44, col=(0, 220, 255, 255), font="3x5")
    draw_text(img, "▲+1.0%", 98, 44, col=(80, 240, 120, 255), font="3x5")
    draw_text(img, "SYS: 60FPS", 56, 54, col=(120, 140, 170, 255), font="3x5")
    return img

# 2. SPOTIFY ENGINE (src/engines/SpotifyEngine.cpp)
def render_spotify():
    img, draw = create_base_canvas()
    # Left: 52x52 Album Artwork at imgX = 2, imgY = 6
    draw.rectangle([2, 6, 53, 57], fill=(20, 15, 35, 255), outline=(30, 45, 35, 255))
    for y in range(7, 57):
        col = (int(160 + (y-7)*1.5), int(40 + (y-7)*2), int(120 - (y-7)*1.5), 255)
        draw.line([3, y, 52, y], fill=col)
    draw.ellipse([18, 18, 38, 38], fill=(255, 220, 40, 255))
    for gy in range(30, 54, 4):
        draw.line([6, gy, 50, gy], fill=(20, 15, 35, 255), width=1)

    # Right: Title, Artist, Visualizer (textX = 58)
    draw_text(img, "STARBOY", 58, 8, col=(255, 255, 255, 255), font="5x7")
    draw_text(img, "THE WEEKND", 58, 22, col=(30, 215, 96, 255), font="3x5")

    # Animated mini equalizer bars at x = 114, y = 34
    bars = [6, 12, 16, 9]
    for bi, bh in enumerate(bars):
        draw.rectangle([112 + bi*3, 38 - bh, 113 + bi*3, 38], fill=(30, 215, 96, 255))

    # Progress bar at y = 48
    draw.rectangle([58, 48, 124, 50], fill=(35, 45, 55, 255))
    draw.rectangle([58, 48, 101, 50], fill=(30, 215, 96, 255)) # 65%
    draw_text(img, "02:34 / 03:50", 58, 54, col=(140, 150, 165, 255), font="3x5")
    return img

# 3. GOOGLE CAST ENGINE (src/engines/GoogleCastEngine.cpp)
def render_googlecast():
    img, draw = create_base_canvas()
    # Left: 52x52 Album Artwork at imgX = 2, imgY = 6
    draw.rectangle([2, 6, 53, 57], fill=(25, 25, 30, 255), outline=(40, 40, 50, 255))
    draw.ellipse([8, 12, 48, 52], fill=(220, 180, 50, 255))
    draw.ellipse([22, 26, 34, 38], fill=(18, 18, 22, 255))

    # Right: Title, Device Name (textX = 58)
    draw.rectangle([58, 8, 64, 13], outline=(66, 133, 244, 255))
    draw_text(img, "LIVING ROOM", 68, 8, col=(66, 133, 244, 255), font="3x5")
    draw_text(img, "GET LUCKY", 58, 19, col=(255, 255, 255, 255), font="5x7")
    draw_text(img, "DAFT PUNK", 58, 31, col=(180, 195, 220, 255), font="3x5")

    # Volume & Progress bar at y = 48
    draw_text(img, "VOL 75%", 58, 42, col=(255, 200, 50, 255), font="3x5")
    draw.rectangle([58, 48, 124, 50], fill=(35, 45, 55, 255))
    draw.rectangle([58, 48, 98, 50], fill=(66, 133, 244, 255))
    draw_text(img, "02:45 / 04:08", 58, 54, col=(130, 145, 165, 255), font="3x5")
    return img

# 4. MUSIC ENGINE (src/engines/MusicEngine.cpp)
def render_music():
    img, draw = create_base_canvas()
    # Left: 30x30 Artwork at x = 2, y = 2
    draw.rectangle([2, 2, 32, 32], fill=(16, 16, 20, 255), outline=(50, 50, 60, 255))
    cx, cy = 17, 17
    draw.ellipse([cx-12, cy-12, cx+12, cy+12], fill=(20, 20, 25, 255), outline=(45, 45, 55, 255))
    draw.ellipse([cx-4, cy-4, cx+4, cy+4], fill=(240, 80, 40, 255))

    # Right of Art (leftMargin = 38): Badge, Title, Artist
    draw_text(img, "[WEBRADIO]", 38, 3, col=(255, 140, 0, 255), font="3x5")
    draw_text(img, "NIGHTCALL", 38, 11, col=(255, 255, 255, 255), font="5x7")
    draw_text(img, "KAVINSKY", 38, 22, col=(100, 200, 255, 255), font="3x5")

    # Progress bar at y = 32 (Rect: 4, 28, w-8, 4 in 64h)
    draw.rectangle([4, 32, 124, 34], fill=(35, 45, 55, 255))
    draw.rectangle([4, 32, 82, 34], fill=(255, 140, 0, 255))

    # Spectrum visualizer bars at y = 38..60 (Rect: 4, 38, w-8, 22 in 64h)
    heights = [6, 12, 18, 15, 21, 18, 14, 19, 12, 16, 10, 8, 14, 18, 20, 17, 13, 16, 11, 7, 12, 15, 18, 13, 9, 6]
    for bi, bh in enumerate(heights):
        bx = 6 + bi * 4
        for y in range(60 - bh, 60):
            ratio = (60 - y) / 22.0
            r = int(255 * ratio)
            g = int(255 * (1.0 - ratio * 0.5))
            b = int(40)
            draw.line([bx, y, bx + 2, y], fill=(r, g, b, 255))
        draw.line([bx, 58 - bh, bx + 2, 58 - bh], fill=(255, 255, 255, 255)) # Peak hold dot
    return img

# 5. SYSINFO ENGINE (src/engines/SysInfoEngine.cpp - Widescreen 2 Balanced Columns)
def render_sysinfo():
    img, draw = create_base_canvas()
    # Column 1 (x = 4..58)
    draw_text(img, "CPU", 4, 10, col=(140, 155, 185, 255), font="3x5")
    draw.rectangle([24, 10, 44, 15], fill=(25, 30, 40, 255), outline=(50, 55, 70, 255))
    draw.rectangle([25, 11, 30, 14], fill=(0, 235, 120, 255))
    draw_text(img, "24%", 48, 10, col=(0, 235, 120, 255), font="3x5")

    draw_text(img, "RAM", 4, 26, col=(140, 155, 185, 255), font="3x5")
    draw.rectangle([24, 26, 44, 31], fill=(25, 30, 40, 255), outline=(50, 55, 70, 255))
    draw.rectangle([25, 27, 35, 30], fill=(0, 235, 120, 255))
    draw_text(img, "48%", 48, 26, col=(0, 235, 120, 255), font="3x5")

    # Column 2 (x = 68..124)
    draw_text(img, "TMP", 68, 10, col=(140, 155, 185, 255), font="3x5")
    draw.rectangle([88, 10, 108, 15], fill=(25, 30, 40, 255), outline=(50, 55, 70, 255))
    draw.rectangle([89, 11, 98, 14], fill=(0, 235, 120, 255))
    draw_text(img, "42°C", 111, 10, col=(0, 235, 120, 255), font="3x5")

    draw_text(img, "UPT", 68, 26, col=(140, 155, 185, 255), font="3x5")
    draw_text(img, "4D 18H", 88, 26, col=(0, 190, 255, 255), font="3x5")

    # Bottom status bar
    draw.line([4, 42, 124, 42], fill=(40, 50, 70, 255))
    draw_text(img, "WIFI: -54 DBM", 4, 48, col=(0, 235, 120, 255), font="3x5")
    draw_text(img, "FREE: 284 KB", 68, 48, col=(180, 195, 220, 255), font="3x5")
    return img

# 6. CRYPTO ENGINE (src/engines/CryptoEngine.cpp - renderUnifiedWide)
def render_crypto():
    img, draw = create_base_canvas()
    # Left column: x = 4..57
    draw.ellipse([4, 4, 19, 19], fill=(247, 147, 26, 255), outline=(255, 200, 80, 255))
    draw_text(img, "B", 8, 7, col=(255, 255, 255, 255), font="5x7")

    draw_text(img, "BTC", 24, 4, col=(255, 255, 255, 255), font="5x7")
    draw_text(img, "24H", 24, 13, col=(140, 140, 140, 255), font="3x5")

    draw_text(img, "$84,320", 4, 24, col=(255, 215, 0, 255), font="5x7")
    draw_text(img, "▲ +5.40%", 4, 35, col=(0, 255, 120, 255), font="5x7")
    draw_text(img, "BINANCE", 4, 52, col=(120, 130, 150, 255), font="3x5")

    # Vertical divider at divX = 58
    draw.line([58, 4, 58, 58], fill=(50, 50, 50, 255))

    # Sparkline chart area on the right: sparkX = 62, sparkY = 6, sparkW = 62, sparkH = 52
    spark_pts = [
        (62, 48), (66, 46), (70, 44), (74, 45), (78, 40), (82, 38),
        (86, 39), (90, 32), (94, 34), (98, 28), (102, 26), (106, 22),
        (110, 24), (114, 18), (118, 16), (124, 12)
    ]
    for i in range(len(spark_pts) - 1):
        x1, y1 = spark_pts[i]
        x2, y2 = spark_pts[i+1]
        draw.line([(x1, y1), (x2, y2)], fill=(0, 255, 120, 255), width=2)
        for fy in range(y1, 58):
            draw.point((x1, fy), fill=(0, int(20 + (58-fy)*0.8), int(10 + (58-fy)*0.3), 255))
    return img

# 7. STOCK ENGINE (src/engines/StockEngine.cpp - renderUnifiedWide)
def render_stock():
    img, draw = create_base_canvas()
    # Left column: x = 4..57
    draw.rectangle([4, 4, 19, 19], fill=(30, 40, 55, 255), outline=(70, 80, 100, 255))
    draw_text(img, "APL", 5, 8, col=(240, 240, 250, 255), font="3x5")

    draw_text(img, "AAPL", 24, 4, col=(255, 255, 255, 255), font="5x7")
    draw_text(img, "1D", 24, 13, col=(140, 140, 140, 255), font="3x5")

    draw_text(img, "$228.45", 4, 24, col=(0, 220, 255, 255), font="5x7")
    draw_text(img, "▲ +0.96%", 4, 35, col=(0, 255, 120, 255), font="5x7")
    draw_text(img, "NASDAQ", 4, 52, col=(120, 130, 150, 255), font="3x5")

    # Vertical divider at divX = 58
    draw.line([58, 4, 58, 58], fill=(50, 50, 50, 255))

    # Sparkline chart area on the right: sparkX = 62, sparkY = 6, sparkW = 62, sparkH = 52
    spark_pts = [
        (62, 38), (66, 36), (70, 40), (74, 39), (78, 35), (82, 34),
        (86, 31), (90, 32), (94, 29), (98, 27), (102, 24), (106, 26),
        (110, 22), (114, 20), (118, 17), (124, 14)
    ]
    for i in range(len(spark_pts) - 1):
        x1, y1 = spark_pts[i]
        x2, y2 = spark_pts[i+1]
        draw.line([(x1, y1), (x2, y2)], fill=(0, 255, 120, 255), width=2)
        for fy in range(y1, 58):
            draw.point((x1, fy), fill=(0, int(20 + (58-fy)*0.8), int(10 + (58-fy)*0.3), 255))
    return img

# 8. WEATHER ENGINE (src/engines/renderers/WeatherLayout.cpp)
def render_weather():
    img, draw = create_base_canvas()
    # WeatherLayout: 24x24 icon on left (WeatherIcon), label + desc in middle, top + bottom temp right-aligned
    # Icon at x = 8, y = 20
    draw.ellipse([14, 16, 28, 30], fill=(255, 210, 30, 255)) # Sun
    draw.ellipse([8, 24, 28, 40], fill=(160, 180, 210, 255))  # Cloud
    draw.ellipse([18, 20, 36, 40], fill=(190, 210, 240, 255))

    # Middle Column: label at y = 18, desc at y = 36
    draw_text(img, "PARIS", 44, 18, col=(180, 180, 255, 255), font="5x7")
    draw_text(img, "PARTLY CLOUDY", 44, 34, col=(210, 210, 210, 255), font="3x5")

    # Right Column: top temp (orange high) at y = 18, bottom temp (cyan low) at y = 34
    draw_text(img, "22°C", 98, 18, col=(255, 150, 50, 255), font="5x7")
    draw_text(img, "14°C", 98, 34, col=(120, 200, 255, 255), font="5x7")

    # Bottom status
    draw_text(img, "HUM 58% • 12 KM/H", 44, 48, col=(130, 145, 170, 255), font="3x5")
    return img

# 9. TEMP ENGINE (src/engines/TempEngine.cpp - SHTC3 Sensor)
def render_temp():
    img, draw = create_base_canvas()
    # Left: Thermometer Icon + Temp reading
    draw.rectangle([8, 18, 12, 38], fill=(200, 200, 200, 255)) # Outer tube
    draw.ellipse([6, 36, 14, 44], fill=(0, 230, 120, 255))     # Bulb
    draw.rectangle([9, 24, 11, 38], fill=(0, 230, 120, 255))   # Mercury

    draw_text(img, "INDOOR", 20, 14, col=(140, 160, 190, 255), font="3x5")
    draw_text(img, "22.4°C", 20, 26, col=(0, 230, 120, 255), font="5x7")

    # Center divider
    draw.line([64, 10, 64, 46], fill=(35, 45, 60, 255))

    # Right: Water Drop Icon + Humidity reading
    draw.ellipse([74, 26, 82, 38], fill=(0, 200, 255, 255))
    draw.polygon([(78, 18), (74, 28), (82, 28)], fill=(0, 200, 255, 255))

    draw_text(img, "HUMIDITY", 88, 14, col=(140, 160, 190, 255), font="3x5")
    draw_text(img, "48%", 88, 26, col=(0, 200, 255, 255), font="5x7")

    # Bottom status
    draw_text(img, "SHTC3 I2C • COMFORT: OPTIMAL", 12, 50, col=(100, 120, 150, 255), font="3x5")
    return img

# 10. DECIBEL ENGINE (src/engines/DecibelEngine.cpp - SPL Noise Meter)
def render_decibel():
    img, draw = create_base_canvas()
    # 1. VS Fighting Healthbar Gauge on top (y = 0..4)
    draw.rectangle([0, 0, 128, 4], fill=(25, 25, 30, 255))
    fill_w = int((62.0 / 110.0) * 128)
    for x in range(fill_w):
        db_val = (x / 128.0) * 110.0
        if db_val < 20: col = (0, 140, 255, 255)
        elif db_val < 40: col = (0, 220, 80, 255)
        elif db_val < 60: col = (255, 220, 0, 255)
        elif db_val < 80: col = (255, 130, 0, 255)
        else: col = (255, 30, 30, 255)
        draw.line([x, 0, x, 4], fill=col)
    draw.line([fill_w - 1, 0, fill_w - 1, 4], fill=(255, 255, 255, 255))
    draw.line([0, 4, 128, 4], fill=(70, 70, 85, 255))

    # 2. Smiley Icon on Left (14x14 at x = 6, y = 22)
    cx, cy, r = 13, 29, 7
    draw.ellipse([cx-r, cy-r, cx+r, cy+r], fill=(0, 220, 80, 255))
    draw.rectangle([cx-3, cy-3, cx-1, cy], fill=(0, 0, 0, 255))
    draw.rectangle([cx+1, cy-3, cx+3, cy], fill=(0, 0, 0, 255))
    draw.arc([cx-4, cy, cx+4, cy+4], 0, 180, fill=(0, 0, 0, 255))

    # 3. dB Numeric Value (textX = 26)
    draw_text(img, "62 dB", 26, 14, col=(255, 220, 0, 255), font="5x7", scale=2)
    draw_text(img, "STATUS: NORMAL", 26, 36, col=(180, 185, 200, 255), font="5x7")

    # 4. Horizontal Segmented VU Meter on bottom (x = 4, y = 58, w = 120, h = 3)
    draw.rectangle([4, 58, 124, 60], fill=(30, 30, 35, 255), outline=(60, 60, 60, 255))
    vu_fill = int(((62 - 30) / 70.0) * 120)
    draw.rectangle([4, 58, 4 + vu_fill, 60], fill=(255, 220, 0, 255))
    return img

# 11. AUDIO VISUALIZER ENGINE (src/engines/VisualizerEngine.cpp)
def render_audiovisualizer():
    img, draw = create_base_canvas()
    num_bars = 32
    bar_w = 128 // num_bars
    heights = [8, 14, 22, 34, 44, 52, 48, 42, 38, 46, 54, 50, 42, 36, 30, 38, 44, 36, 28, 24, 30, 26, 20, 16, 12, 9, 7, 5, 8, 11, 7, 4]

    for i in range(num_bars):
        bx = i * bar_w
        bh = heights[i]
        for y in range(bh):
            ratio = y / 64.0
            if ratio < 0.4:
                r = 0
                g = int(200 * (ratio / 0.4))
                b = 255
            elif ratio < 0.75:
                sub = (ratio - 0.4) / 0.35
                r = int(255 * sub)
                g = 255
                b = 0
            else:
                sub = (ratio - 0.75) / 0.25
                r = 255
                g = int(50 * (1.0 - sub))
                b = int(200 * sub)
            draw.rectangle([bx, 63 - y, bx + bar_w - 2, 63 - y], fill=(r, g, b, 255))

        peak_y = 63 - bh - 2
        if peak_y >= 0:
            draw.rectangle([bx, peak_y, bx + bar_w - 2, peak_y], fill=(255, 255, 255, 255))
    return img

# 12. GNEWS ENGINE (src/engines/GNewsEngine.cpp)
def render_gnews():
    img, draw = create_base_canvas()
    # 1. Header Bar: Pulsing red live beacon + Category pill badge + Source
    draw.ellipse([4, 6, 8, 10], fill=(255, 25, 40, 255))
    draw.rounded_rectangle([12, 2, 46, 13], radius=2, fill=(20, 25, 35, 255), outline=(0, 220, 255, 255))
    draw_text(img, "TECH", 16, 4, col=(0, 220, 255, 255), font="3x5")
    draw_text(img, "REUTERS", 52, 4, col=(140, 155, 175, 255), font="3x5")
    draw_text(img, "15:42", 102, 4, col=(120, 135, 155, 255), font="3x5")

    # Divider line at divY = 16
    draw.line([0, 16, 128, 16], fill=(50, 60, 80, 255))

    # 2. Wrapped Headline Text in clean white
    draw_text(img, "JAMES WEBB TELESCOPE", 4, 21, col=(255, 255, 255, 255), font="5x7")
    draw_text(img, "FINDS SIGNS OF WATER", 4, 32, col=(255, 255, 255, 255), font="5x7")
    draw_text(img, "ON DISTANT EXOPLANET", 4, 43, col=(255, 255, 255, 255), font="5x7")
    draw_text(img, "HABITABLE ZONE K2-18B", 4, 54, col=(255, 215, 0, 255), font="5x7")
    return img

# 13. MQTT DATA ENGINE (src/engines/MqttDataEngine.cpp & ValuesRenderer.cpp)
def render_mqttdata():
    img, draw = create_base_canvas()
    # Stacked Home Assistant Tiles (ValuesRenderer.cpp)
    # Tile 1: SALON (x = 2..41)
    draw_text(img, "SALON", 10, 10, col=(140, 160, 200, 255), font="3x5")
    draw_text(img, "21.5", 6, 24, col=(80, 240, 120, 255), font="5x7")
    draw_text(img, "°C", 32, 24, col=(120, 200, 255, 255), font="3x5")

    # Vertical divider 1
    draw.line([43, 8, 43, 44], fill=(40, 50, 65, 255))

    # Tile 2: SOLAR (x = 45..85)
    draw_text(img, "SOLAR", 52, 10, col=(140, 160, 200, 255), font="3x5")
    draw_text(img, "3.42", 48, 24, col=(255, 215, 0, 255), font="5x7")
    draw_text(img, "KW", 74, 24, col=(140, 160, 200, 255), font="3x5")

    # Vertical divider 2
    draw.line([85, 8, 85, 44], fill=(40, 50, 65, 255))

    # Tile 3: BATTERY (x = 87..126)
    draw_text(img, "BATT", 96, 10, col=(140, 160, 200, 255), font="3x5")
    draw_text(img, "88", 92, 24, col=(0, 220, 255, 255), font="5x7")
    draw_text(img, "%", 112, 24, col=(140, 160, 200, 255), font="3x5")

    # Bottom status bar
    draw.line([2, 50, 126, 50], fill=(35, 45, 60, 255))
    draw_text(img, "MQTT LIVE • HOME ASSISTANT", 8, 54, col=(100, 120, 150, 255), font="3x5")
    return img

# 14. GIF ENGINE (src/engines/GifEngine.cpp)
def render_gifs():
    img, draw = create_base_canvas()
    # Retro Synthwave Driving Pixel Art Scene
    for y in range(0, 36):
        r = int(60 + (36 - y) * 4)
        g = int(20 + y * 2)
        b = int(80 + y * 3)
        draw.line([0, y, 128, y], fill=(r, g, b, 255))
    draw.ellipse([48, 8, 80, 40], fill=(255, 215, 0, 255))
    for sy in range(20, 36, 3):
        draw.line([48, sy, 80, sy], fill=(120, 30, 90, 255), width=1)
    draw.line([14, 20, 16, 42], fill=(20, 10, 30, 255), width=2)
    draw.arc([6, 12, 24, 28], 180, 320, fill=(20, 10, 30, 255), width=2)
    draw.rectangle([0, 36, 128, 64], fill=(12, 10, 24, 255))
    draw.line([0, 36, 128, 36], fill=(255, 40, 150, 255), width=1)
    for gx in [10, 30, 50, 78, 98, 118]:
        draw.line([gx, 36, 64 + (gx-64)*3, 64], fill=(220, 40, 160, 255))
    draw.rectangle([54, 46, 74, 56], fill=(240, 30, 30, 255))
    draw.rectangle([58, 42, 70, 46], fill=(40, 40, 50, 255))
    draw.rectangle([55, 52, 58, 55], fill=(255, 240, 50, 255))
    draw.rectangle([70, 52, 73, 55], fill=(255, 240, 50, 255))
    return img

# 15. MESSAGE ENGINE (src/engines/MessageEngine.cpp)
def render_message():
    img, draw = create_base_canvas()
    draw.rectangle([4, 8, 123, 55], outline=(200, 150, 30, 255), width=2)
    draw.rectangle([6, 10, 121, 53], fill=(14, 12, 8, 255))
    for rx, ry in [(5, 9), (122, 9), (5, 54), (122, 54)]:
        draw.point((rx, ry), fill=(255, 220, 80, 255))

    draw_text(img, "★ ARCADE MATRIX ★", 10, 18, col=(255, 170, 0, 255), font="5x7")
    draw_text(img, "SYSTEM OPERATIONAL", 10, 32, col=(255, 210, 40, 255), font="5x7")
    draw_text(img, "60 FPS CORE 1 LOCK-FREE", 14, 44, col=(220, 140, 0, 255), font="3x5")
    return img

# 16. MARQUEE ENGINE (src/engines/MarqueeEngine.cpp)
def render_marquee():
    img, draw = create_base_canvas()
    draw.rectangle([2, 6, 125, 57], fill=(10, 14, 25, 255), outline=(50, 60, 85, 255), width=2)
    draw.line([2, 6, 125, 6], fill=(255, 200, 40, 255), width=2)
    draw.line([2, 57, 125, 57], fill=(255, 200, 40, 255), width=2)

    draw_text(img, "SNK NEO-GEO", 26, 14, col=(255, 220, 0, 255), font="5x7", scale=1)
    draw_text(img, "PRO-GEAR SPEC", 28, 24, col=(255, 40, 40, 255), font="5x7", scale=1)

    draw.rectangle([12, 38, 30, 48], fill=(30, 30, 40, 255), outline=(80, 80, 100, 255))
    draw.line([21, 38, 21, 44], fill=(220, 220, 220, 255), width=2)
    draw.ellipse([18, 35, 24, 40], fill=(240, 30, 30, 255))
    draw.ellipse([40, 40, 44, 44], fill=(240, 40, 40, 255))
    draw.ellipse([48, 38, 52, 42], fill=(40, 120, 240, 255))
    draw.ellipse([56, 40, 60, 44], fill=(240, 220, 40, 255))
    draw.ellipse([64, 38, 68, 42], fill=(40, 220, 80, 255))

    draw_text(img, "INSERT COIN", 76, 40, col=(255, 255, 255, 255), font="3x5")
    return img

# 17. DATE ENGINE (src/engines/DateEngine.cpp - Authentic Arcade Date & Shadow)
def render_date():
    img, draw = create_base_canvas()
    for x in range(0, 128, 8):
        draw.line([x, 0, x, 64], fill=(12, 14, 20, 255))
    for y in range(0, 64, 8):
        draw.line([0, y, 128, y], fill=(12, 14, 20, 255))

    # Top Header: "JEUDI • THURSDAY"
    draw_text(img, "JEUDI • THURSDAY", 26, 8, col=(100, 180, 255, 255), font="3x5")

    # Center: Massive Arcade Date with 3D shadow (Capcom theme: Gold on Blue)
    # Perfectly centered at x = 5 (10 chars * 12px - 2px = 118px)
    draw_text_with_shadow(img, "08/10/2026", 5, 22, col=(255, 215, 0, 255), shadow_col=(0, 75, 175, 255), depth=2, font="5x7", scale=2)

    # Subtitle / Sync info at bottom
    draw_text(img, "RTC DS3231 • NTP SYNC", 24, 46, col=(140, 160, 190, 255), font="3x5")
    draw_text(img, "CAPCOM ARCADE STYLE", 26, 54, col=(80, 100, 130, 255), font="3x5")
    return img

# 18. CLOCK ENGINE (src/engines/ClockEngine.cpp - Authentic Arcade Retro Clock)
def render_clock():
    img, draw = create_base_canvas()
    for x in range(0, 128, 8):
        draw.line([x, 0, x, 64], fill=(12, 14, 20, 255))
    for y in range(0, 64, 8):
        draw.line([0, y, 128, y], fill=(12, 14, 20, 255))

    draw_text(img, "ARCADE RETRO CLOCK", 22, 6, col=(255, 140, 0, 255), font="3x5")

    # Giant Time: "10:42:35" with 3D drop shadow
    draw_text_with_shadow(img, "10:42", 12, 18, col=(255, 215, 0, 255), shadow_col=(0, 75, 175, 255), depth=2, font="5x7", scale=3)
    draw_text_with_shadow(img, ":35", 96, 26, col=(255, 60, 60, 255), shadow_col=(100, 0, 0, 255), depth=1, font="5x7", scale=2)

    # Bottom link prompt to dedicated showcase
    draw_text(img, "10+ RETRO CLOCK THEMES BELOW ➔", 4, 48, col=(80, 200, 255, 255), font="3x5")
    return img

# ----------------------------------------------------------------------------
# Master Engine Generator Mapping
# ----------------------------------------------------------------------------
ENGINE_GENERATORS = [
    ("dashboard", "DESK MASTER DASHBOARD", "HUB75 CLOCK", render_dashboard),
    ("spotify", "SPOTIFY NOW PLAYING", "STREAMING MEDIA", render_spotify),
    ("googlecast", "GOOGLE CAST & NEST", "MDNS AUDIO", render_googlecast),
    ("music", "WEBRADIO & MP3 PLAYER", "MINIMP3 DAC", render_music),
    ("sysinfo", "SYSTEM TELEMETRY HUD", "ESP32-S3 SOC", render_sysinfo),
    ("crypto", "CRYPTO PRICE & SPARKLINE", "BINANCE / CG", render_crypto),
    ("stock", "STOCK MARKET TICKER", "YAHOO FINANCE", render_stock),
    ("weather", "DYNAMIC WEATHER FORECAST", "OPENWEATHER", render_weather),
    ("temp", "SHTC3 CLIMATE SENSOR", "I2C SENSOR", render_temp),
    ("decibel", "SPL DECIBEL NOISE METER", "I2S MICROPHONE", render_decibel),
    ("visualizer", "SPECTRUM AUDIO VISUALIZER", "64-PT FFT", render_audiovisualizer),
    ("gnews", "LIVE BREAKING NEWS TICKER", "GNEWS HTTPS", render_gnews),
    ("mqttdata", "HOME ASSISTANT & MQTT", "IOT BROKER", render_mqttdata),
    ("gif", "ANIMATED RETRO GIF PLAYER", "SD-CARD SPI", render_gifs),
    ("message", "SCROLLING MARQUEE BANNER", "60 FPS CORE 1", render_message),
    ("marquee", "PIXELCADE ARCADE MARQUEE", "BATOCERA RETRO", render_marquee),
    ("date", "CALENDAR & DATE ENGINE", "NTP RTC SYNC", render_date),
    ("clock", "RETRO GAME & ARCADE CLOCK", "NTP RTC SYNC", render_clock),
]

def generate_all():
    print(f"Generating 100% C++ aligned simulated screens for {len(ENGINE_GENERATORS)} engines...")
    for engine_id, title, badge, gen_fn in ENGINE_GENERATORS:
        raw = gen_fn()
        led = apply_led_matrix_shader(raw, led_pitch=4, led_radius=1)
        screen = wrap_with_hardware_bezel(led, title=title, badge=badge)
        out_path = os.path.join(ENGINES_ASSET_DIR, f"engine_{engine_id}.png")
        screen.save(out_path)
        print(f"  ✓ engine_{engine_id}.png ({screen.width}x{screen.height})")

if __name__ == "__main__":
    generate_all()
