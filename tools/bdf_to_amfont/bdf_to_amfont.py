#!/usr/bin/env python3
"""
bdf_to_amfont.py - Convert a BDF bitmap font into ArcadeMatrix's runtime-loadable
".amf" (AMFONT) binary format, for use with BitmapFontLoader on the ESP32 firmware.

Why BDF? It's the same font format the ArcadeMatrix_RPi project already ships
(fonts/*.bdf, loaded at runtime via rgbmatrix's graphics.Font.LoadFont()), it is a
strict per-pixel bitmap format (no antialiasing/hinting), and it's trivial to parse
with no external dependencies - a good match for LED matrix displays and for a
microcontroller with no font-rendering library beyond Adafruit GFX.

The .amf format mirrors Adafruit GFX's compiled-in GFXfont layout (bitmapOffset,
width, height, xAdvance, xOffset, yOffset per glyph + a packed, MSB-first bitstream
of glyph bitmaps, each glyph individually byte-aligned - exactly matching what
Adafruit's own fontconvert tool produces) so BitmapFontLoader can reconstruct a
GFXfont-compatible struct in RAM at boot, with matrix->setFont() working unmodified.

Usage:
    python3 bdf_to_amfont.py input.bdf output.amf [--first 0x20] [--last 0x7E]

The default glyph range (0x20 '  ' to 0x7E '~') covers printable ASCII, matching
the range used by ArcadeMatrix's existing compiled-in fonts.
"""
import argparse
import struct
import sys

MAGIC = b"AMF1"


def parse_bdf(path):
    """Parse a BDF file into a dict of {encoding: glyph_dict} plus global metrics."""
    glyphs = {}
    default_yadvance = None

    with open(path, "r", encoding="latin-1") as f:
        lines = f.readlines()

    i = 0
    n = len(lines)
    while i < n:
        line = lines[i].strip()
        if line.startswith("FONTBOUNDINGBOX"):
            parts = line.split()
            # FONTBOUNDINGBOX width height xoff yoff
            default_yadvance = int(parts[2])
        elif line.startswith("STARTCHAR"):
            glyph = {"encoding": None, "dwidth": 0, "bbx": (0, 0, 0, 0), "bitmap": []}
            i += 1
            while i < n and not lines[i].strip().startswith("ENDCHAR"):
                gline = lines[i].strip()
                if gline.startswith("ENCODING"):
                    glyph["encoding"] = int(gline.split()[1])
                elif gline.startswith("DWIDTH"):
                    parts = gline.split()
                    glyph["dwidth"] = int(parts[1])
                elif gline.startswith("BBX"):
                    parts = gline.split()
                    # BBX width height xoff yoff
                    glyph["bbx"] = (int(parts[1]), int(parts[2]), int(parts[3]), int(parts[4]))
                elif gline.startswith("BITMAP"):
                    i += 1
                    bw = glyph["bbx"][0]
                    row_bytes = (bw + 7) // 8
                    while i < n and not lines[i].strip().startswith("ENDCHAR"):
                        hex_row = lines[i].strip()
                        if not hex_row:
                            i += 1
                            continue
                        row_val = int(hex_row, 16)
                        glyph["bitmap"].append((row_val, row_bytes))
                        i += 1
                    continue
                i += 1
            if glyph["encoding"] is not None and glyph["encoding"] >= 0:
                glyphs[glyph["encoding"]] = glyph
        i += 1

    if default_yadvance is None:
        default_yadvance = 16  # sane fallback
    return glyphs, default_yadvance


def pack_bits(bitstream):
    """Pack a list of individual bits (0/1, MSB-first within each byte) into bytes."""
    out = bytearray()
    cur = 0
    count = 0
    for bit in bitstream:
        cur = (cur << 1) | (bit & 1)
        count += 1
        if count == 8:
            out.append(cur)
            cur = 0
            count = 0
    if count > 0:
        cur <<= (8 - count)
        out.append(cur)
    return bytes(out)


def glyph_bits(glyph):
    """Extract a flat, row-major, MSB-first bit list for one glyph's BBX-sized bitmap."""
    bw, bh, _, _ = glyph["bbx"]
    bits = []
    for row_val, row_bytes in glyph["bitmap"]:
        # BDF hex rows are already MSB-first within the declared bounding box width,
        # but each row is padded to a whole byte boundary - only take the first bw bits.
        total_bits = row_bytes * 8
        row_bits = [(row_val >> (total_bits - 1 - b)) & 1 for b in range(total_bits)]
        bits.extend(row_bits[:bw])
    # Defensive: pad/truncate to exactly bw*bh bits in case of malformed BDF rows.
    expected = bw * bh
    if len(bits) < expected:
        bits.extend([0] * (expected - len(bits)))
    return bits[:expected]


def convert(bdf_path, out_path, first, last):
    glyphs, yadvance = parse_bdf(bdf_path)

    glyph_entries = []
    bitmap_bytes_out = bytearray()
    missing = 0
    for code in range(first, last + 1):
        g = glyphs.get(code)
        if g is None:
            # Emit a zero-width blank glyph rather than failing the whole font.
            glyph_entries.append({
                "bitmapOffset": len(bitmap_bytes_out),
                "width": 0, "height": 0, "xAdvance": 0, "xOffset": 0, "yOffset": 0,
            })
            missing += 1
            continue
        bw, bh, bxoff, byoff = g["bbx"]
        bits = glyph_bits(g)
        glyph_entries.append({
            "bitmapOffset": len(bitmap_bytes_out),
            "width": bw, "height": bh,
            "xAdvance": g["dwidth"],
            "xOffset": bxoff,
            # GFXfont yOffset convention: negative = above baseline. BDF's BBX yoff is the
            # offset from baseline to the bitmap's bottom row, so top-of-glyph offset is:
            "yOffset": -(byoff + bh),
        })
        # Adafruit_GFX's drawChar() resets its bit cursor to 0 for every glyph and always
        # starts reading at bitmap[glyph->bitmapOffset] - i.e. each glyph's packed bits must
        # start on its OWN byte boundary (glyphs are NOT bit-packed contiguously across each
        # other, only within their own w*h bits). pack_bits() below pads each glyph's leftover
        # bits up to the next byte, matching Adafruit's fontconvert tool exactly.
        bitmap_bytes_out.extend(pack_bits(bits))

    glyph_count = last - first + 1

    with open(out_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<HHBBH", first, last, yadvance & 0xFF, 0, glyph_count))
        for ge in glyph_entries:
            f.write(struct.pack(
                "<IBBBbb",
                ge["bitmapOffset"],
                ge["width"], ge["height"], ge["xAdvance"],
                ge["xOffset"], ge["yOffset"],
            ))
        f.write(bytes(bitmap_bytes_out))

    total = 12 + glyph_count * 11 + len(bitmap_bytes_out)
    print(f"Wrote {out_path}: {glyph_count} glyphs ({missing} missing/blank), "
          f"{len(bitmap_bytes_out)} bitmap bytes, {total} bytes total.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input_bdf")
    ap.add_argument("output_amf")
    ap.add_argument("--first", type=lambda x: int(x, 0), default=0x20,
                     help="First glyph codepoint (default 0x20, space)")
    ap.add_argument("--last", type=lambda x: int(x, 0), default=0x7E,
                     help="Last glyph codepoint (default 0x7E, '~')")
    args = ap.parse_args()

    if args.first > args.last:
        sys.exit("--first must be <= --last")

    convert(args.input_bdf, args.output_amf, args.first, args.last)


if __name__ == "__main__":
    main()
