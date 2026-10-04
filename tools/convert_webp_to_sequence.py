#!/usr/bin/env python3
"""
Convert an animated webp/gif into a ProtoTracer ImageSequence header.

Usage: convert_webp_to_sequence.py <input.webp> <ClassName> <output.h> [--size 94]

Output header is palette-indexed (PROGMEM) and inherits from ImageSequence.
Path-correct includes for the actual repo layout (../../Animation -> wrong upstream;
real is ../Utils/ImageSequence.h since this header lives in Animated/Giphy/).
"""
import sys
import argparse
from PIL import Image

def read_all_frames(path):
    img = Image.open(path)
    frames = []
    i = 0
    try:
        while True:
            img.seek(i)
            f = img.convert("RGBA")
            frames.append(f)
            i += 1
    except EOFError:
        pass
    return frames

def composite_on_white(rgba):
    """Flatten RGBA to RGB on white background (LED panel reads black as off, so
    keep the background readable)."""
    bg = Image.new("RGB", rgba.size, (0, 0, 0))
    bg.paste(rgba, mask=rgba.split()[3])
    return bg

def quantize_with_shared_palette(frames_rgb, n_colors=64):
    """Build a single palette across all frames so the sequence has consistent
    colors and we can dedupe the palette table."""
    combined = Image.new("RGB", (frames_rgb[0].width * len(frames_rgb), frames_rgb[0].height))
    for i, f in enumerate(frames_rgb):
        combined.paste(f, (i * frames_rgb[0].width, 0))
    pal_img = combined.convert("P", palette=Image.ADAPTIVE, colors=n_colors)
    palette = pal_img.getpalette()[: n_colors * 3]
    while len(palette) < n_colors * 3:
        palette.extend([0, 0, 0])
    out = []
    for f in frames_rgb:
        out.append(f.quantize(palette=pal_img, dither=Image.FLOYDSTEINBERG))
    return out, palette

def emit_header(class_name, w, h, palette, frames_indexed):
    n_colors = len(palette) // 3
    n_frames = len(frames_indexed)

    lines = []
    lines.append("#pragma once\n")
    lines.append('#include "../Utils/ImageSequence.h"\n\n')

    lines.append(f"class {class_name}Sequence : public ImageSequence {{\n")
    lines.append("private:\n")
    for i in range(n_frames):
        lines.append(f"    static const uint8_t frame{i:04d}[];\n")
    lines.append(f"    static const uint8_t* sequence[{n_frames}];\n")
    lines.append("    static const uint8_t rgbColors[];\n\n")
    lines.append(f"    Image image = Image(frame0000, rgbColors, {w}, {h}, {n_colors - 1});\n\n")
    lines.append("public:\n")
    lines.append(f"    {class_name}Sequence(Vector2D size, Vector2D offset, float fps)\n")
    lines.append(f"        : ImageSequence(&image, sequence, (unsigned int){n_frames}, fps) {{\n")
    lines.append("        image.SetSize(size);\n")
    lines.append("        image.SetPosition(offset);\n")
    lines.append("    }\n")
    lines.append("};\n\n")

    for i, f in enumerate(frames_indexed):
        data = list(f.getdata())
        lines.append(f"const uint8_t {class_name}Sequence::frame{i:04d}[] PROGMEM = {{")
        lines.append(",".join(str(x) for x in data))
        lines.append("};\n")

    lines.append(f"\nconst uint8_t* {class_name}Sequence::sequence[] = {{")
    lines.append(",".join(f"{class_name}Sequence::frame{i:04d}" for i in range(n_frames)))
    lines.append("};\n")

    lines.append(f"const uint8_t {class_name}Sequence::rgbColors[] PROGMEM = {{")
    rgb_parts = []
    for i in range(n_colors):
        r, g, b = palette[i * 3 : i * 3 + 3]
        rgb_parts.append(f"{r},{g},{b}")
    lines.append(",".join(rgb_parts))
    lines.append("};\n")

    return "".join(lines)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("class_name")
    ap.add_argument("output")
    ap.add_argument("--size", type=int, default=94)
    ap.add_argument("--colors", type=int, default=64)
    args = ap.parse_args()

    raw = read_all_frames(args.input)
    rgb = [composite_on_white(f).resize((args.size, args.size), Image.LANCZOS) for f in raw]
    indexed, palette = quantize_with_shared_palette(rgb, n_colors=args.colors)

    header = emit_header(args.class_name, args.size, args.size, palette, indexed)
    with open(args.output, "w") as f:
        f.write(header)

    print(f"{args.input} -> {args.output} ({len(raw)} frames, {args.size}x{args.size}, {args.colors} colors)")

if __name__ == "__main__":
    main()
