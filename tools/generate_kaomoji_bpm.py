#!/usr/bin/env python3
"""
Genera headers de kaomojis cycling con flash + fadeout a BPM custom y color
fosfo (pink o green). 6 kaomojis × 1 beat c/u = 54 frames per loop.

Beat math:
  frames_per_beat = 9 (consistente)
  140 BPM @ 21.0 fps:   9 frames × 47.6ms = 0.4286 s/beat ✓ exacto
  128 BPM @ 19.2 fps:   9 frames × 52.0ms = 0.469 s/beat ✓ exacto

Usage:
  generate_kaomoji_bpm.py output.h --class-name KaoPink140 --color pink --bpm 140
"""
import argparse
from PIL import Image, ImageDraw

PANEL_W, PANEL_H = 64, 32
FRAMES_PER_BEAT = 6
# 140 BPM @ 14.0 fps:  6 frames × 71.4ms = 0.4286 s/beat ✓
# 128 BPM @ 12.8 fps:  6 frames × 78.1ms = 0.469 s/beat ✓
BLACK = (0, 0, 0)

PINK_FOSFO  = (255, 60, 200)
GREEN_FOSFO = (80, 255, 110)

# Posiciones canónicas (ya validadas en el slot KAOMOJI existente)
PAREN_TOP = 5
PAREN_BOT = 26
EYE_L_CX = 18
EYE_R_CX = 46
EYE_CY   = 13
MOUTH_CX = 32
MOUTH_CY = 19


# ---------- PRIMITIVES ----------

def draw_paren_open(d, color):
    d.line([(5, PAREN_TOP + 1), (5, PAREN_BOT - 1)], fill=color, width=1)
    d.line([(4, PAREN_TOP + 3), (4, PAREN_BOT - 3)], fill=color, width=1)
    d.line([(6, PAREN_TOP), (6, PAREN_TOP + 1)], fill=color, width=1)
    d.line([(6, PAREN_BOT - 1), (6, PAREN_BOT)], fill=color, width=1)


def draw_paren_close(d, color):
    d.line([(58, PAREN_TOP + 1), (58, PAREN_BOT - 1)], fill=color, width=1)
    d.line([(59, PAREN_TOP + 3), (59, PAREN_BOT - 3)], fill=color, width=1)
    d.line([(57, PAREN_TOP), (57, PAREN_TOP + 1)], fill=color, width=1)
    d.line([(57, PAREN_BOT - 1), (57, PAREN_BOT)], fill=color, width=1)


def eye_circle(d, cx, cy, color):
    d.ellipse([(cx - 3, cy - 3), (cx + 3, cy + 3)], fill=color)
    d.point((cx + 1, cy - 1), fill=BLACK)


def eye_heart(d, cx, cy, color):
    d.point((cx - 2, cy - 2), fill=color); d.point((cx - 1, cy - 2), fill=color)
    d.point((cx + 1, cy - 2), fill=color); d.point((cx + 2, cy - 2), fill=color)
    d.line([(cx - 3, cy - 1), (cx - 3, cy)], fill=color)
    d.line([(cx + 3, cy - 1), (cx + 3, cy)], fill=color)
    d.line([(cx - 2, cy + 1), (cx + 2, cy + 1)], fill=color)
    d.line([(cx - 1, cy + 2), (cx + 1, cy + 2)], fill=color)
    d.point((cx, cy + 3), fill=color)
    d.line([(cx - 2, cy), (cx + 2, cy)], fill=color)
    d.line([(cx - 1, cy - 1), (cx + 1, cy - 1)], fill=color)


def eye_squint_left(d, cx, cy, color):
    d.line([(cx - 3, cy - 2), (cx + 1, cy)], fill=color, width=1)
    d.line([(cx + 1, cy), (cx - 3, cy + 2)], fill=color, width=1)
    d.line([(cx - 3, cy + 3), (cx + 1, cy + 3)], fill=color)


def eye_squint_right(d, cx, cy, color):
    d.line([(cx + 3, cy - 2), (cx - 1, cy)], fill=color, width=1)
    d.line([(cx - 1, cy), (cx + 3, cy + 2)], fill=color, width=1)
    d.line([(cx - 1, cy + 3), (cx + 3, cy + 3)], fill=color)


def eye_sparkle(d, cx, cy, color):
    d.point((cx, cy - 3), fill=color); d.point((cx, cy + 3), fill=color)
    d.point((cx - 3, cy), fill=color); d.point((cx + 3, cy), fill=color)
    d.point((cx, cy - 2), fill=color); d.point((cx, cy + 2), fill=color)
    d.point((cx - 2, cy), fill=color); d.point((cx + 2, cy), fill=color)
    d.point((cx, cy), fill=color)
    d.point((cx - 1, cy - 1), fill=color); d.point((cx + 1, cy + 1), fill=color)
    d.point((cx - 1, cy + 1), fill=color); d.point((cx + 1, cy - 1), fill=color)


def eye_apostrophe_left(d, cx, cy, color):
    d.line([(cx - 1, cy - 2), (cx + 1, cy + 1)], fill=color, width=1)
    d.line([(cx, cy - 2), (cx + 2, cy + 1)], fill=color, width=1)


def eye_apostrophe_right(d, cx, cy, color):
    d.line([(cx + 1, cy - 2), (cx - 1, cy + 1)], fill=color, width=1)
    d.line([(cx, cy - 2), (cx - 2, cy + 1)], fill=color, width=1)


def eye_teary_left(d, cx, cy, color):
    """ojo cerrado con lágrima — > shape with tear below."""
    d.line([(cx - 3, cy - 2), (cx + 1, cy + 1)], fill=color, width=1)
    d.line([(cx + 1, cy + 1), (cx - 3, cy + 3)], fill=color, width=1)
    # tear drop debajo
    d.point((cx - 1, cy + 4), fill=color)


def eye_teary_right(d, cx, cy, color):
    d.line([(cx + 3, cy - 2), (cx - 1, cy + 1)], fill=color, width=1)
    d.line([(cx - 1, cy + 1), (cx + 3, cy + 3)], fill=color, width=1)
    d.point((cx + 1, cy + 4), fill=color)


def mouth_smile_small(d, cx, cy, color):
    d.point((cx - 3, cy - 1), fill=color)
    d.point((cx + 3, cy - 1), fill=color)
    d.line([(cx - 2, cy), (cx + 2, cy)], fill=color)


def mouth_smile_wide(d, cx, cy, color):
    d.point((cx - 4, cy - 2), fill=color); d.point((cx + 4, cy - 2), fill=color)
    d.point((cx - 3, cy - 1), fill=color); d.point((cx + 3, cy - 1), fill=color)
    d.line([(cx - 3, cy), (cx + 3, cy)], fill=color)
    d.line([(cx - 2, cy + 1), (cx + 2, cy + 1)], fill=color)


def mouth_omega(d, cx, cy, color):
    d.point((cx - 3, cy - 1), fill=color)
    d.point((cx - 3, cy), fill=color)
    d.point((cx - 2, cy + 1), fill=color)
    d.point((cx - 1, cy), fill=color)
    d.point((cx, cy - 1), fill=color)
    d.point((cx + 1, cy), fill=color)
    d.point((cx + 2, cy + 1), fill=color)
    d.point((cx + 3, cy), fill=color)
    d.point((cx + 3, cy - 1), fill=color)


def mouth_wavy_sad(d, cx, cy, color):
    """﹏ wavy mouth (sad/concerned)."""
    d.point((cx - 3, cy + 1), fill=color)
    d.point((cx - 2, cy), fill=color)
    d.point((cx - 1, cy + 1), fill=color)
    d.point((cx, cy), fill=color)
    d.point((cx + 1, cy + 1), fill=color)
    d.point((cx + 2, cy), fill=color)
    d.point((cx + 3, cy + 1), fill=color)


# ---------- KAOMOJI COMPOSITIONS ----------

def render_kao_1(d, color):
    """(◕‿◕) happy."""
    draw_paren_open(d, color); draw_paren_close(d, color)
    eye_circle(d, EYE_L_CX, EYE_CY, color)
    eye_circle(d, EYE_R_CX, EYE_CY, color)
    mouth_smile_small(d, MOUTH_CX, MOUTH_CY, color)


def render_kao_2(d, color):
    """(♥‿♥) in love."""
    draw_paren_open(d, color); draw_paren_close(d, color)
    eye_heart(d, EYE_L_CX, EYE_CY, color)
    eye_heart(d, EYE_R_CX, EYE_CY, color)
    mouth_smile_small(d, MOUTH_CX, MOUTH_CY, color)


def render_kao_3(d, color):
    """(≧◡≦) laughing."""
    draw_paren_open(d, color); draw_paren_close(d, color)
    eye_squint_left(d, EYE_L_CX, EYE_CY, color)
    eye_squint_right(d, EYE_R_CX, EYE_CY, color)
    mouth_smile_wide(d, MOUTH_CX, MOUTH_CY, color)


def render_kao_4(d, color):
    """(✧ω✧) sparkly playful."""
    draw_paren_open(d, color); draw_paren_close(d, color)
    eye_sparkle(d, EYE_L_CX, EYE_CY, color)
    eye_sparkle(d, EYE_R_CX, EYE_CY, color)
    mouth_omega(d, MOUTH_CX, MOUTH_CY, color)


def render_kao_5(d, color):
    """(´◡`) soft happy."""
    draw_paren_open(d, color); draw_paren_close(d, color)
    eye_apostrophe_left(d, EYE_L_CX, EYE_CY, color)
    eye_apostrophe_right(d, EYE_R_CX, EYE_CY, color)
    mouth_smile_wide(d, MOUTH_CX, MOUTH_CY, color)


def render_kao_6(d, color):
    """(>﹏<) teary eyes."""
    draw_paren_open(d, color); draw_paren_close(d, color)
    eye_teary_left(d, EYE_L_CX, EYE_CY, color)
    eye_teary_right(d, EYE_R_CX, EYE_CY, color)
    mouth_wavy_sad(d, MOUTH_CX, MOUTH_CY, color)


# 4 kaomojis para que el firmware total quepa en el limite del teensy_loader_cli
# (65k líneas hex). Selección representativa: happy / love / laugh / sparkle.
KAOMOJI_FUNCS = [render_kao_1, render_kao_2, render_kao_3, render_kao_4]


# ---------- FRAME BUILDER ----------

def build_frames(color):
    """6 kaomojis × FRAMES_PER_BEAT frames cada uno con linear fade."""
    frames = []
    n_total = len(KAOMOJI_FUNCS) * FRAMES_PER_BEAT
    for f in range(n_total):
        kao_idx = f // FRAMES_PER_BEAT
        in_beat = f % FRAMES_PER_BEAT
        intensity = 1.0 - 0.95 * (in_beat / (FRAMES_PER_BEAT - 1))
        c = (
            max(1, int(color[0] * intensity)),
            max(1, int(color[1] * intensity)),
            max(1, int(color[2] * intensity)),
        )
        img = Image.new("RGB", (PANEL_W, PANEL_H), BLACK)
        d = ImageDraw.Draw(img)
        KAOMOJI_FUNCS[kao_idx](d, c)
        frames.append(img)
    return frames


# ---------- ENCODE ----------

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
    ap.add_argument("output")
    ap.add_argument("--class-name", required=True)
    ap.add_argument("--color", choices=["pink", "green"], required=True)
    ap.add_argument("--preview-dir", default=None)
    args = ap.parse_args()

    color = PINK_FOSFO if args.color == "pink" else GREEN_FOSFO
    frames = build_frames(color)

    if args.preview_dir:
        import os
        os.makedirs(args.preview_dir, exist_ok=True)
        for i, f in enumerate(frames):
            f.resize((PANEL_W * 6, PANEL_H * 6), Image.NEAREST).save(f"{args.preview_dir}/k_{i:02d}.png")

    indexed, palette = quantize_shared(frames)
    header = emit_header(args.class_name, PANEL_W, PANEL_H, palette, indexed)
    with open(args.output, "w") as f:
        f.write(header)
    print(f"-> {args.output} ({len(frames)} frames, color={args.color})")


if __name__ == "__main__":
    main()
