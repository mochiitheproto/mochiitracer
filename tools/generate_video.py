#!/usr/bin/env python3
"""
Convierte un directorio de frames PNG (ya 64x32, extraídos con ffmpeg) en un
slot ImageSequence con LOOP SEAMLESS (crossfade de la cola hacia la cabeza, así
el final empalma suave con el inicio). colors = 3*n_colors - 2 (fix GetRGB).

Usage:
  generate_video.py /tmp/vidframes out.h --class-name Ojos --overlap 18 --n-colors 80 --preview-dir /tmp/p
"""
import argparse, glob, os
import numpy as np
from PIL import Image

PANEL_W, PANEL_H = 64, 32


def load_frames(d):
    paths = sorted(glob.glob(os.path.join(d, "*.png")))
    frames = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        if im.size != (PANEL_W, PANEL_H):
            im = im.resize((PANEL_W, PANEL_H), Image.LANCZOS)
        frames.append(np.asarray(im).astype(np.float32))
    return frames


def seamless_loop(frames, overlap):
    """Loop sin costura: crossfade de los últimos `overlap` frames (la cola)
    hacia los primeros, de modo que out[M-1] -> out[0] sea continuo."""
    N = len(frames)
    O = min(overlap, N // 3)
    M = N - O
    out = []
    for i in range(M):
        if i < O:
            a = i / O                       # 0 -> casi 1
            f = (1.0 - a) * frames[N - O + i] + a * frames[i]
        else:
            f = frames[i]
        out.append(Image.fromarray(np.clip(f, 0, 255).astype(np.uint8)))
    return out


def quantize_shared(frames, n_colors):
    combined = Image.new("RGB", (PANEL_W * len(frames), PANEL_H))
    for i, fr in enumerate(frames):
        combined.paste(fr, (i * PANEL_W, 0))
    pal_img = combined.convert("P", palette=Image.ADAPTIVE, colors=n_colors)
    palette = pal_img.getpalette()[: n_colors * 3]
    while len(palette) < n_colors * 3:
        palette.extend([0, 0, 0])
    indexed = [fr.quantize(palette=pal_img, dither=Image.NONE) for fr in frames]
    return indexed, palette


def emit_header(class_name, w, h, palette, frames_indexed):
    n_colors = len(palette) // 3
    colors_arg = 3 * n_colors - 2
    assert colors_arg <= 255, f"n_colors {n_colors} -> {colors_arg} > 255"
    n_frames = len(frames_indexed)
    L = ["#pragma once\n", '#include "../Utils/ImageSequence.h"\n\n']
    L.append(f"class {class_name}Sequence : public ImageSequence {{\n private:\n")
    for i in range(n_frames):
        L.append(f"    static const uint8_t frame{i:04d}[];\n")
    L.append(f"    static const uint8_t* sequence[{n_frames}];\n")
    L.append("    static const uint8_t rgbColors[];\n\n")
    L.append(f"    Image image = Image(frame0000, rgbColors, {w}, {h}, {colors_arg});\n\n public:\n")
    L.append(f"    {class_name}Sequence(Vector2D size, Vector2D offset, float fps)\n")
    L.append(f"        : ImageSequence(&image, sequence, (unsigned int){n_frames}, fps) {{\n")
    L.append("        image.SetSize(size);\n        image.SetPosition(offset);\n    }\n};\n\n")
    for i, fr in enumerate(frames_indexed):
        data = list(fr.getdata())
        L.append(f"const uint8_t {class_name}Sequence::frame{i:04d}[] PROGMEM = {{")
        L.append(",".join(str(x) for x in data))
        L.append("};\n")
    L.append(f"\nconst uint8_t* {class_name}Sequence::sequence[] = {{")
    L.append(",".join(f"{class_name}Sequence::frame{i:04d}" for i in range(n_frames)))
    L.append("};\n")
    L.append(f"const uint8_t {class_name}Sequence::rgbColors[] PROGMEM = {{")
    L.append(",".join(f"{palette[i*3]},{palette[i*3+1]},{palette[i*3+2]}" for i in range(n_colors)))
    L.append("};\n")
    return "".join(L)


def save_preview(frames, preview_dir, tag):
    os.makedirs(preview_dir, exist_ok=True)
    cols = 8
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (PANEL_W * 3 + 4), rows * (PANEL_H * 3 + 4)), (30, 30, 30))
    for i, fr in enumerate(frames):
        r, c = divmod(i, cols)
        sheet.paste(fr.resize((PANEL_W * 3, PANEL_H * 3), Image.NEAREST),
                    (c * (PANEL_W * 3 + 4) + 2, r * (PANEL_H * 3 + 4) + 2))
    sheet.save(f"{preview_dir}/{tag}_contact.png")
    frames[0].resize((PANEL_W * 5, PANEL_H * 5), Image.NEAREST).save(
        f"{preview_dir}/{tag}.gif", save_all=True,
        append_images=[fr.resize((PANEL_W * 5, PANEL_H * 5), Image.NEAREST) for fr in frames[1:]],
        duration=90, loop=0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames_dir")
    ap.add_argument("output")
    ap.add_argument("--class-name", required=True)
    ap.add_argument("--overlap", type=int, default=18)
    ap.add_argument("--n-colors", type=int, default=80)
    ap.add_argument("--preview-dir", default=None)
    args = ap.parse_args()

    raw = load_frames(args.frames_dir)
    print(f"frames cargados: {len(raw)}")
    loop = seamless_loop(raw, args.overlap)
    print(f"loop seamless: {len(loop)} frames (overlap {args.overlap})")
    if args.preview_dir:
        save_preview(loop, args.preview_dir, args.class_name)
    indexed, palette = quantize_shared(loop, args.n_colors)
    hdr = emit_header(args.class_name, PANEL_W, PANEL_H, palette, indexed)
    with open(args.output, "w") as f:
        f.write(hdr)
    print(f"-> {args.output} | {len(loop)} frames, n_colors={args.n_colors}, "
          f"~{len(loop)*PANEL_W*PANEL_H} data bytes (~{len(loop)*PANEL_W*PANEL_H//16} hex lines)")


if __name__ == "__main__":
    main()
