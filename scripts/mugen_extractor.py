import os
import sys
import struct
import argparse
from PIL import Image, ImageSequence

def rgb_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def process_gif(input_path, output_path):
    print(f"Processing {input_path}...")
    img = Image.open(input_path)
    
    frames = [frame.copy().convert('RGBA') for frame in ImageSequence.Iterator(img)]
    if not frames:
        return
    
    width, height = frames[0].size
    frame_count = len(frames)
    
    # We use MAGENTA (FF00FF) as our transparent chroma key, 
    # which is RGB565: 0xF81F
    TRANS_COLOR = 0xF81F
    
    # Collect delays
    delays = []
    for frame in ImageSequence.Iterator(img):
        # durations in ms, fallback to 100ms
        dur = frame.info.get('duration', 100) 
        delays.append(dur)
        
    with open(output_path, 'wb') as f:
        # Magic + Version
        f.write(b'FGT\x01')
        # Header: width, height, frames, transparent color
        f.write(struct.pack('<HHHH', width, height, frame_count, TRANS_COLOR))
        
        # Write Delays
        f.write(struct.pack(f'<{frame_count}H', *delays))
        
        # Write Pixels for each frame
        for frame in frames:
            pixels = frame.load()
            frame_data = bytearray(width * height * 2)
            idx = 0
            for y in range(height):
                for x in range(width):
                    r, g, b, a = pixels[x, y]
                    if a < 128:
                        c = TRANS_COLOR
                    else:
                        c = rgb_to_rgb565(r, g, b)
                    frame_data[idx] = c & 0xFF
                    frame_data[idx+1] = (c >> 8) & 0xFF
                    idx += 2
            f.write(frame_data)
    
    print(f"Saved: {output_path} (Frames: {frame_count}, {width}x{height})")

def main():
    parser = argparse.ArgumentParser(description="MUGEN / Sprite to .fgt Converter")
    parser.add_argument('-i', '--input', required=True, help="Input file (.gif) or directory")
    parser.add_argument('-o', '--output', required=True, help="Output directory")
    args = parser.parse_args()
    
    if not os.path.exists(args.output):
        os.makedirs(args.output)
        
    if os.path.isfile(args.input):
        out_file = os.path.join(args.output, os.path.splitext(os.path.basename(args.input))[0] + ".fgt")
        process_gif(args.input, out_file)
    elif os.path.isdir(args.input):
        for f in os.listdir(args.input):
            if f.lower().endswith('.gif'):
                in_path = os.path.join(args.input, f)
                out_path = os.path.join(args.output, os.path.splitext(f)[0] + ".fgt")
                process_gif(in_path, out_path)

if __name__ == "__main__":
    main()
