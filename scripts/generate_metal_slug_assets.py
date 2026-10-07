import os
from PIL import Image

base = '/Users/red1l/Documents/work/git/perso/Metal-Slug/Metal_Slug'
scratch = '/Users/red1l/.gemini/antigravity-ide/brain/b0237323-e014-4f1c-a2cd-051699331dfd/scratch'
out_file = '/Users/red1l/Documents/work/git/perso/ArcadeMatrix/src/engines/clocks/MetalSlugAssets.h'

MASK = 0x000E

def to_565(r, g, b, a):
    if a < 128:
        return MASK
    val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    if val == MASK:
        val = 0x0010
    return val

def image_to_cpp(img, name, comment=""):
    w, h = img.size
    pixels = img.convert('RGBA').load()
    out = []
    if comment:
        out.append(f"// {comment}")
    out.append(f"constexpr int {name}_W = {w};")
    out.append(f"constexpr int {name}_H = {h};")
    out.append(f"static const uint16_t {name}[{w * h}] PROGMEM = {{")
    
    rows = []
    for y in range(h):
        row_vals = []
        for x in range(w):
            r, g, b, a = pixels[x, y]
            row_vals.append(f"0x{to_565(r, g, b, a):04X}")
        rows.append("    " + ", ".join(row_vals) + ",")
    out.append("\n".join(rows))
    out.append("};\n")
    return "\n".join(out)

def bg_to_cpp(img, name):
    w, h = img.size
    pixels = img.convert('RGB').load()
    out = []
    out.append(f"constexpr int {name}_W = {w};")
    out.append(f"constexpr int {name}_H = {h};")
    out.append(f"static const uint16_t {name}[{w * h}] PROGMEM = {{")
    rows = []
    for y in range(h):
        row_vals = []
        for x in range(w):
            r, g, b = pixels[x, y]
            val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            row_vals.append(f"0x{val:04X}")
        rows.append("    " + ", ".join(row_vals) + ",")
    out.append("\n".join(rows))
    out.append("};\n")
    return "\n".join(out)

header_code = []
header_code.append("""#ifndef METALSLUGASSETS_H
#define METALSLUGASSETS_H

#include <Arduino.h>

/**
 * Authentic SNK Neo Geo Metal Slug Artwork & Sprites:
 * - Mission 2 Arabian Desert / Warzone 256x64 Night Backdrop
 * - Marco Rossi: Idle breathing, Walking, Shooting with muzzle flash, Grenade, Victory
 * - Rebel Army Di-Cokka Tank: Active combat stance, Shell projectile, Wrecked stance
 * - Rebel Flying Helicopter: Aerial patrol with rotor
 * - Fiery Explosive Fireballs (4 blast stages)
 * Stored in Flash PROGMEM (RGB565, MASK = 0x000E).
 */

namespace MetalSlugAssets {

constexpr uint16_t MASK = 0x000E;

// Arcade 3x5 Font for Metal Slug HUD & Digits
static const uint8_t ARCADE_FONT_3x5[11][5] = {
    { 0x07, 0x05, 0x05, 0x05, 0x07 }, // '0'
    { 0x02, 0x06, 0x02, 0x02, 0x07 }, // '1'
    { 0x07, 0x01, 0x07, 0x04, 0x07 }, // '2'
    { 0x07, 0x01, 0x07, 0x01, 0x07 }, // '3'
    { 0x05, 0x05, 0x07, 0x01, 0x01 }, // '4'
    { 0x07, 0x04, 0x07, 0x01, 0x07 }, // '5'
    { 0x07, 0x04, 0x07, 0x05, 0x07 }, // '6'
    { 0x07, 0x01, 0x02, 0x02, 0x02 }, // '7'
    { 0x07, 0x05, 0x07, 0x05, 0x07 }, // '8'
    { 0x07, 0x05, 0x07, 0x01, 0x07 }, // '9'
    { 0x00, 0x02, 0x00, 0x02, 0x00 }  // ':'
};
""")

# 1. Background
bg = Image.open(f'{scratch}/ground_x500.png').convert('RGB')
header_code.append(bg_to_cpp(bg, "BG_DESERT_256x64"))

# 2. Marco Rossi
header_code.append(image_to_cpp(Image.open(f'{base}/PlayerIdle/0.png'), "MARCO_IDLE_0", "Marco Idle Frame 0"))
header_code.append(image_to_cpp(Image.open(f'{base}/PlayerIdle/4.png'), "MARCO_IDLE_1", "Marco Idle Frame 1"))
header_code.append(image_to_cpp(Image.open(f'{base}/PlayerWalking/0.png'), "MARCO_WALK_0", "Marco Walking Frame 0"))
header_code.append(image_to_cpp(Image.open(f'{base}/PlayerWalking/6.png'), "MARCO_WALK_1", "Marco Walking Frame 1"))
header_code.append(image_to_cpp(Image.open(f'{base}/PlayerShooting/0.png'), "MARCO_SHOOT_0", "Marco Heavy Machine Gun Fire Frame 0"))
header_code.append(image_to_cpp(Image.open(f'{base}/PlayerShooting/1.png'), "MARCO_SHOOT_1", "Marco Heavy Machine Gun Muzzle Flash Frame 1"))
header_code.append(image_to_cpp(Image.open(f'{base}/PlayerGrenade/2.png'), "MARCO_GRENADE", "Marco Throwing Grenade"))
header_code.append(image_to_cpp(Image.open(f'{base}/PlayerVictory/2.png'), "MARCO_VICTORY", "Marco Thumbs Up Victory Yell"))

# 3. Enemy Tank Di-Cokka
tank_alive = Image.open(f'{base}/EnemyTank/EnemyTank.png')
header_code.append(image_to_cpp(tank_alive, "TANK_ALIVE", "Rebel Army Di-Cokka Tank"))

tank_death_raw = Image.open(f'{base}/EnemyTank/TankDeath.png')
tank_dead = tank_death_raw.crop((12, 6, 83, 62)) # 71x56
header_code.append(image_to_cpp(tank_dead, "TANK_DEAD", "Wrecked Smoking Di-Cokka Tank"))

bullet = Image.open(f'{base}/EnemyTank/TankBullet.png')
header_code.append(image_to_cpp(bullet, "TANK_BULLET", "Di-Cokka Cannon Shell"))

# 4. Enemy Helicopter
heli_raw = Image.open(f'{base}/EnemyHelicopter/EnemyHelicopter.png')
heli = heli_raw.resize((45, 33), Image.NEAREST)
header_code.append(image_to_cpp(heli, "ENEMY_HELI", "Rebel Patrol Helicopter"))

# 5. Explosions
exp0 = Image.open(f'{base}/ProjectileGrenadeExplosion/1.png').resize((36, 48), Image.NEAREST)
exp1 = Image.open(f'{base}/ProjectileGrenadeExplosion/4.png').resize((36, 48), Image.NEAREST)
exp2 = Image.open(f'{base}/ProjectileGrenadeExplosion/8.png').resize((36, 48), Image.NEAREST)
exp3 = Image.open(f'{base}/ProjectileGrenadeExplosion/14.png').resize((36, 48), Image.NEAREST)

header_code.append(image_to_cpp(exp0, "EXPLOSION_0", "Fireball Explosion Blast Frame 0"))
header_code.append(image_to_cpp(exp1, "EXPLOSION_1", "Fireball Explosion Blast Frame 1"))
header_code.append(image_to_cpp(exp2, "EXPLOSION_2", "Fireball Explosion Blast Frame 2"))
header_code.append(image_to_cpp(exp3, "EXPLOSION_3", "Fireball Explosion Blast Frame 3"))

header_code.append("""} // namespace MetalSlugAssets

#endif // METALSLUGASSETS_H
""")

with open(out_file, 'w') as f:
    f.write("\n".join(header_code))

print(f"Successfully generated {out_file}, size: {os.path.getsize(out_file)} bytes")
