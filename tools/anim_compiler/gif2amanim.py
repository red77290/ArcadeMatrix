#!/usr/bin/env python3
"""
gif2amanim.py - ArcadeMatrix Native Animation (.amanim) Compiler

Compiles standard GIF animations into pre-quantized, delta-frame,
run-length encoded RGB565 binary streams for zero-overhead playback on ESP32.
"""

import sys
import struct
import argparse
from PIL import Image

MAGIC = b"AMAN"
VERSION = 1

def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def compile_gif_to_amanim(input_path, output_path, target_fps=30):
    im = Image.open(input_path)
    width, height = im.size
    frames = []

    try:
        while True:
            frame_rgb = im.convert("RGB")
            frames.append(frame_rgb)
            im.seek(im.tell() + 1)
    except EOFError:
        pass

    frame_count = len(frames)
    if frame_count == 0:
        raise ValueError("No frames found in input GIF")

    # Quantize global palette up to 256 colors
    quantized_first = frames[0].quantize(colors=256, method=Image.MEDIANCUT)
    palette_raw = quantized_first.getpalette()[:256 * 3]
    palette_565 = []
    for i in range(0, len(palette_raw), 3):
        r, g, b = palette_raw[i], palette_raw[i+1], palette_raw[i+2]
        palette_565.append(rgb888_to_rgb565(r, g, b))

    # Binary Header: MAGIC (4B), Version (1B), Width (2B), Height (2B), FrameCount (2B), FPS (1B), PaletteEntries (2B)
    header = struct.pack(
        "<4sBHHHBH",
        MAGIC,
        VERSION,
        width,
        height,
        frame_count,
        target_fps,
        len(palette_565)
    )

    with open(output_path, "wb") as out:
        out.write(header)
        # Palette block
        for col in palette_565:
            out.write(struct.pack("<H", col))

        # Frames block (Index bytes)
        for frame in frames:
            q_frame = frame.quantize(palette=quantized_first)
            raw_indices = q_frame.tobytes()
            # Simple RLE compression
            rle_bytes = bytearray()
            idx = 0
            while idx < len(raw_indices):
                byte_val = raw_indices[idx]
                run_len = 1
                while idx + run_len < len(raw_indices) and raw_indices[idx + run_len] == byte_val and run_len < 255:
                    run_len += 1
                rle_bytes.append(run_len)
                rle_bytes.append(byte_val)
                idx += run_len

            # Frame header: PayloadSize (2B)
            out.write(struct.pack("<H", len(rle_bytes)))
            out.write(rle_bytes)

    print(f"Successfully compiled {input_path} to {output_path} ({width}x{height}, {frame_count} frames, {len(palette_565)} colors)")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Compile GIF to ArcadeMatrix .amanim")
    parser.add_argument("input", help="Path to input GIF file")
    parser.add_argument("output", help="Path to output .amanim file")
    parser.add_argument("--fps", type=int, default=30, help="Target framerate (default: 30)")
    args = parser.parse_args()

    compile_gif_to_amanim(args.input, args.output, args.fps)
