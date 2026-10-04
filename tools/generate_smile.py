#!/usr/bin/env python3
"""
Genera Smile<N>.h — caritas kawaii pixel-art a 140 BPM.
Modos:
  - strobe: ON full / OFF total cada beat (sin fade)
  - fade:   ON full + linear decay dentro del beat

Beat math:
  140 BPM = 0.4286 s/beat
  21 fps × 0.4286 ≈ 9 frames/beat
  4 beats/loop = 36 frames @ 21 fps = 1.71 s/loop

Strobe schedule (per beat, 9 frames):
  frame 0..1: ON (2 frames de flash brillante)
  frame 2..8: OFF (7 frames de negro)

Fade schedule (per beat):
  frame 0: full bright
  frame N: intensity = 1.0 - 0.95 * (N / 8)
"""
import argparse
from PIL import Image

PANEL_W, PANEL_H = 64, 32
SAFE_W, SAFE_H = 60, 28
SAFE_X0, SAFE_Y0 = 2, 2

FRAMES_PER_BEAT = 9
BEATS_PER_LOOP  = 4
N_FRAMES        = FRAMES_PER_BEAT * BEATS_PER_LOOP

BLACK = (0, 0, 0)


def load_pixel_art(src_path):
    """Carga imagen y la fittea a safe zone preservando pixel-art con BOX averaging."""
    src = Image.open(src_path).convert("RGB")
    sw, sh = src.size
    # Aspect-fit
    scale = min(SAFE_W / sw, SAFE_H / sh)
    nw = max(1, int(sw * scale))
    nh = max(1, int(sh * scale))
    # BOX downsample preserva aristas pixel-art mejor que LANCZOS para HD pixel-art
    resized = src.resize((nw, nh), Image.BOX)

    canvas = Image.new("RGB", (PANEL_W, PANEL_H), BLACK)
    cx0 = (PANEL_W - nw) // 2
    cy0 = (PANEL_H - nh) // 2
    canvas.paste(resized, (cx0, cy0))
    return canvas


def apply_intensity(img, intensity):
    out = Image.new("RGB", img.size, BLACK)
    src = img.load()
    dst = out.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b = src[x, y]
            if (r, g, b) == BLACK:
                continue
            dst[x, y] = (
                max(1, int(r * intensity)),
                max(1, int(g * intensity)),
                max(1, int(b * intensity)),
            )
    return out


def build_frames_strobe(base):
    """ON full × 2 frames + OFF × 7 frames per beat."""
    black_img = Image.new("RGB", (PANEL_W, PANEL_H), BLACK)
    frames = []
    for f in range(N_FRAMES):
        in_beat = f % FRAMES_PER_BEAT
        frames.append(base if in_beat < 2 else black_img)
    return frames


def build_frames_fade(base):
    frames = []
    for f in range(N_FRAMES):
        in_beat = f % FRAMES_PER_BEAT
        intensity = 1.0 - 0.95 * (in_beat / (FRAMES_PER_BEAT - 1))
        frames.append(apply_intensity(base, intensity))
    return frames


def quantize_shared(frames, n_colors=12):
    combined = Image.new("RGB", (PANEL_W * len(frames), PANEL_H))
    for i, f in enumerate(frames):
        combined.paste(f, (i * PANEL_W, 0))
    pal_img = combined.convert("P", palette=Image.ADAPTIVE, colors=n_colors)
    palette = pal_img.getpalette()[: n_colors * 3]
    while len(palette) < n_colors * 3:
        palette.extend([0, 0, 0])
    indexed = [f.quantize(palette=pal_img, dither=Image.NONE) for f in frames]
    return indexed, palette


def emit_header(class_name, w, h, palette, frames_indexed):
    n_colors = len(palette) // 3
    n_frames = len(frames_indexed)
    L = []
    L.append("#pragma once\n")
    L.append('#include "../Utils/ImageSequence.h"\n\n')
    L.append(f"class {class_name}Sequence : public ImageSequence {{\n")
    L.append("private:\n")
    for i in range(n_frames):
        L.append(f"    static const uint8_t frame{i:04d}[];\n")
    L.append(f"    static const uint8_t* sequence[{n_frames}];\n")
    L.append("    static const uint8_t rgbColors[];\n\n")
    L.append(f"    Image image = Image(frame0000, rgbColors, {w}, {h}, {n_colors});\n\n")
    L.append("public:\n")
    L.append(f"    {class_name}Sequence(Vector2D size, Vector2D offset, float fps)\n")
    L.append(f"        : ImageSequence(&image, sequence, (unsigned int){n_frames}, fps) {{\n")
    L.append("        image.SetSize(size);\n")
    L.append("        image.SetPosition(offset);\n")
    L.append("    }\n")
    L.append("};\n\n")
    for i, f in enumerate(frames_indexed):
        data = list(f.getdata())
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("output")
    ap.add_argument("--class-name", required=True)
    ap.add_argument("--mode", choices=["strobe", "fade"], required=True)
    ap.add_argument("--preview-dir", default=None)
    args = ap.parse_args()

    base = load_pixel_art(args.src)
    frames = build_frames_strobe(base) if args.mode == "strobe" else build_frames_fade(base)

    if args.preview_dir:
        import os
        os.makedirs(args.preview_dir, exist_ok=True)
        for i, f in enumerate(frames):
            f.resize((PANEL_W * 6, PANEL_H * 6), Image.NEAREST).save(f"{args.preview_dir}/sm_{i:02d}.png")

    indexed, palette = quantize_shared(frames)
    header = emit_header(args.class_name, PANEL_W, PANEL_H, palette, indexed)
    with open(args.output, "w") as f:
        f.write(header)
    print(f"-> {args.output} ({len(frames)} frames @ 21fps = 140 BPM, mode={args.mode})")


if __name__ == "__main__":
    main()
