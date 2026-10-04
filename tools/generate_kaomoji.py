#!/usr/bin/env python3
"""
Genera Kaomoji.h — secuencia animada de kaomojis pixel-art (joy/amor/jugueteo)
con flashes blancos full-screen estroboscópicos entre cada uno.

Kaomojis incluidos:
  1. (◕‿◕)    happy clásico
  2. (♥‿♥)    in love
  3. (≧◡≦)    riéndose
  4. (✧ω✧)    sparkly playful
  5. (´◡`)    soft happy
  6. (｡◕‿◕｡)  cute happy con dots

Frame schedule (a 18 fps):
  kaomoji 1 (2 frames) → white flash (1) → kaomoji 2 (2) → white flash (1) → ...
  Total: 6 kaomojis × 3 = 18 frames. Loop 1s.

Layout safe zone X=2..61, Y=2..29.
"""
import argparse
from PIL import Image, ImageDraw

PANEL_W, PANEL_H = 64, 32
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

# Posiciones canónicas de los componentes del kaomoji
PAREN_L_X = 4    # X start of opening (
PAREN_R_X = 56   # X start of closing )
PAREN_TOP = 5
PAREN_BOT = 26

EYE_L_CX = 18    # centro X ojo izq
EYE_R_CX = 46    # centro X ojo der
EYE_CY   = 13    # centro Y ojos

MOUTH_CX = 32
MOUTH_CY = 19

# ---------- PAREN PRIMITIVES ----------

def draw_paren_open(d, color):
    """ ( -- vertical line en X=4 + curva. Y=5..26 (alto)."""
    # tronco vertical principal (curvado simulado por offset)
    d.line([(5, PAREN_TOP + 1), (5, PAREN_BOT - 1)], fill=color, width=1)
    d.line([(4, PAREN_TOP + 3), (4, PAREN_BOT - 3)], fill=color, width=1)
    d.line([(6, PAREN_TOP), (6, PAREN_TOP + 1)], fill=color, width=1)
    d.line([(6, PAREN_BOT - 1), (6, PAREN_BOT)], fill=color, width=1)


def draw_paren_close(d, color):
    """ ) -- mirror del open. X=56..58."""
    d.line([(58, PAREN_TOP + 1), (58, PAREN_BOT - 1)], fill=color, width=1)
    d.line([(59, PAREN_TOP + 3), (59, PAREN_BOT - 3)], fill=color, width=1)
    d.line([(57, PAREN_TOP), (57, PAREN_TOP + 1)], fill=color, width=1)
    d.line([(57, PAREN_BOT - 1), (57, PAREN_BOT)], fill=color, width=1)


# ---------- EYE PRIMITIVES (5×5 a 7×7) ----------

def eye_circle(d, cx, cy, color):
    """ ◕ ojo redondo lleno con highlight."""
    d.ellipse([(cx - 3, cy - 3), (cx + 3, cy + 3)], fill=color)
    # Highlight (apaga un pixel adentro)
    d.point((cx + 1, cy - 1), fill=BLACK)


def eye_heart(d, cx, cy, color):
    """ ♥ ojo en forma de corazón."""
    # dos pequeños bumps arriba + V abajo
    d.point((cx - 2, cy - 2), fill=color); d.point((cx - 1, cy - 2), fill=color)
    d.point((cx + 1, cy - 2), fill=color); d.point((cx + 2, cy - 2), fill=color)
    d.line([(cx - 3, cy - 1), (cx - 3, cy)], fill=color)
    d.line([(cx + 3, cy - 1), (cx + 3, cy)], fill=color)
    d.line([(cx - 2, cy + 1), (cx + 2, cy + 1)], fill=color)
    d.line([(cx - 1, cy + 2), (cx + 1, cy + 2)], fill=color)
    d.point((cx, cy + 3), fill=color)
    # interior
    d.line([(cx - 2, cy), (cx + 2, cy)], fill=color)
    d.line([(cx - 1, cy - 1), (cx + 1, cy - 1)], fill=color)


def eye_squint_left(d, cx, cy, color):
    """ ≧ ojo apretado (izq) — dos diagonales hacia abajo-derecha como >."""
    # > shape: top-left to center-right, then center-right to bottom-left
    d.line([(cx - 3, cy - 2), (cx + 1, cy)], fill=color, width=1)
    d.line([(cx + 1, cy), (cx - 3, cy + 2)], fill=color, width=1)
    # punto inferior (raya horizontal)
    d.line([(cx - 3, cy + 3), (cx + 1, cy + 3)], fill=color)


def eye_squint_right(d, cx, cy, color):
    """ ≦ ojo apretado (der) — espejo de squint_left."""
    d.line([(cx + 3, cy - 2), (cx - 1, cy)], fill=color, width=1)
    d.line([(cx - 1, cy), (cx + 3, cy + 2)], fill=color, width=1)
    d.line([(cx - 1, cy + 3), (cx + 3, cy + 3)], fill=color)


def eye_sparkle(d, cx, cy, color):
    """ ✧ estrella / sparkle de 4 puntas."""
    d.point((cx, cy - 3), fill=color); d.point((cx, cy + 3), fill=color)
    d.point((cx - 3, cy), fill=color); d.point((cx + 3, cy), fill=color)
    d.point((cx, cy - 2), fill=color); d.point((cx, cy + 2), fill=color)
    d.point((cx - 2, cy), fill=color); d.point((cx + 2, cy), fill=color)
    d.point((cx, cy), fill=color)
    d.point((cx - 1, cy - 1), fill=color); d.point((cx + 1, cy + 1), fill=color)
    d.point((cx - 1, cy + 1), fill=color); d.point((cx + 1, cy - 1), fill=color)


def eye_apostrophe_left(d, cx, cy, color):
    """ ´ comma-style angled mark, leans left (para soft happy)."""
    d.line([(cx - 1, cy - 2), (cx + 1, cy + 1)], fill=color, width=1)
    d.line([(cx, cy - 2), (cx + 2, cy + 1)], fill=color, width=1)


def eye_apostrophe_right(d, cx, cy, color):
    """ ` mark angled right."""
    d.line([(cx + 1, cy - 2), (cx - 1, cy + 1)], fill=color, width=1)
    d.line([(cx, cy - 2), (cx - 2, cy + 1)], fill=color, width=1)


# ---------- MOUTH PRIMITIVES ----------

def mouth_smile_small(d, cx, cy, color):
    """ ‿ pequeña curva sonrisa."""
    d.point((cx - 3, cy - 1), fill=color)
    d.point((cx + 3, cy - 1), fill=color)
    d.line([(cx - 2, cy), (cx + 2, cy)], fill=color)


def mouth_smile_wide(d, cx, cy, color):
    """ ◡ sonrisa más ancha."""
    d.point((cx - 4, cy - 2), fill=color); d.point((cx + 4, cy - 2), fill=color)
    d.point((cx - 3, cy - 1), fill=color); d.point((cx + 3, cy - 1), fill=color)
    d.line([(cx - 3, cy), (cx + 3, cy)], fill=color)
    d.line([(cx - 2, cy + 1), (cx + 2, cy + 1)], fill=color)


def mouth_omega(d, cx, cy, color):
    """ ω boquita gatuna."""
    # dos arcos pegados
    d.point((cx - 3, cy - 1), fill=color)
    d.point((cx - 3, cy), fill=color)
    d.point((cx - 2, cy + 1), fill=color)
    d.point((cx - 1, cy), fill=color)
    d.point((cx, cy - 1), fill=color)  # pico central
    d.point((cx + 1, cy), fill=color)
    d.point((cx + 2, cy + 1), fill=color)
    d.point((cx + 3, cy), fill=color)
    d.point((cx + 3, cy - 1), fill=color)


# ---------- KAOMOJI COMPOSITIONS ----------

def render_kaomoji_1(d):
    """(◕‿◕) happy clásico."""
    draw_paren_open(d, WHITE)
    eye_circle(d, EYE_L_CX, EYE_CY, WHITE)
    eye_circle(d, EYE_R_CX, EYE_CY, WHITE)
    mouth_smile_small(d, MOUTH_CX, MOUTH_CY, WHITE)
    draw_paren_close(d, WHITE)


def render_kaomoji_2(d):
    """(♥‿♥) in love."""
    draw_paren_open(d, WHITE)
    eye_heart(d, EYE_L_CX, EYE_CY, WHITE)
    eye_heart(d, EYE_R_CX, EYE_CY, WHITE)
    mouth_smile_small(d, MOUTH_CX, MOUTH_CY, WHITE)
    draw_paren_close(d, WHITE)


def render_kaomoji_3(d):
    """(≧◡≦) laughing."""
    draw_paren_open(d, WHITE)
    eye_squint_left(d, EYE_L_CX, EYE_CY, WHITE)
    eye_squint_right(d, EYE_R_CX, EYE_CY, WHITE)
    mouth_smile_wide(d, MOUTH_CX, MOUTH_CY, WHITE)
    draw_paren_close(d, WHITE)


def render_kaomoji_4(d):
    """(✧ω✧) sparkly playful."""
    draw_paren_open(d, WHITE)
    eye_sparkle(d, EYE_L_CX, EYE_CY, WHITE)
    eye_sparkle(d, EYE_R_CX, EYE_CY, WHITE)
    mouth_omega(d, MOUTH_CX, MOUTH_CY, WHITE)
    draw_paren_close(d, WHITE)


def render_kaomoji_5(d):
    """(´◡`) soft happy."""
    draw_paren_open(d, WHITE)
    eye_apostrophe_left(d, EYE_L_CX, EYE_CY, WHITE)
    eye_apostrophe_right(d, EYE_R_CX, EYE_CY, WHITE)
    mouth_smile_wide(d, MOUTH_CX, MOUTH_CY, WHITE)
    draw_paren_close(d, WHITE)


def render_kaomoji_6(d):
    """(｡◕‿◕｡) cute happy con dots — muy joyful."""
    draw_paren_open(d, WHITE)
    # dots ｡ (small dot+stroke)
    d.point((9, 23), fill=WHITE); d.point((10, 23), fill=WHITE)
    d.point((9, 24), fill=WHITE); d.point((10, 24), fill=WHITE)
    eye_circle(d, EYE_L_CX, EYE_CY, WHITE)
    eye_circle(d, EYE_R_CX, EYE_CY, WHITE)
    mouth_smile_small(d, MOUTH_CX, MOUTH_CY, WHITE)
    # dots ｡ (right side)
    d.point((53, 23), fill=WHITE); d.point((54, 23), fill=WHITE)
    d.point((53, 24), fill=WHITE); d.point((54, 24), fill=WHITE)
    draw_paren_close(d, WHITE)


KAOMOJIS = [
    render_kaomoji_1,
    render_kaomoji_2,
    render_kaomoji_3,
    render_kaomoji_4,
    render_kaomoji_5,
    render_kaomoji_6,
]


# ---------- FRAME SEQUENCE ----------

def render_kaomoji_frame(render_fn):
    img = Image.new("RGB", (PANEL_W, PANEL_H), BLACK)
    d = ImageDraw.Draw(img)
    render_fn(d)
    return img


def render_white_flash():
    return Image.new("RGB", (PANEL_W, PANEL_H), WHITE)


def build_frames():
    """Cada kaomoji visible 2 frames + 1 flash blanco (estrobo).
    Total 6 × 3 = 18 frames. A 18 fps → loop 1s."""
    frames = []
    for k in KAOMOJIS:
        kao = render_kaomoji_frame(k)
        frames.append(kao)
        frames.append(kao)        # 2 frames del kaomoji
        frames.append(render_white_flash())  # 1 frame blanco
    return frames


# ---------- ENCODE ----------

def quantize(frames):
    palette_rgb = [BLACK, WHITE, BLACK, BLACK]  # padding to 4
    flat = [c for col in palette_rgb for c in col]
    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette(flat + [0] * (768 - len(flat)))
    indexed = [f.quantize(palette=pal_img, dither=Image.NONE) for f in frames]
    return indexed, flat


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
    # `colors=n_colors` (no n_colors-1) — workaround del bug en Image::GetRGB
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
    ap.add_argument("--preview-dir", default=None)
    args = ap.parse_args()

    frames = build_frames()
    if args.preview_dir:
        import os
        os.makedirs(args.preview_dir, exist_ok=True)
        for i, f in enumerate(frames):
            f.resize((PANEL_W * 6, PANEL_H * 6), Image.NEAREST).save(f"{args.preview_dir}/kao_{i:02d}.png")
    indexed, palette = quantize(frames)
    header = emit_header("Kaomoji", PANEL_W, PANEL_H, palette, indexed)
    with open(args.output, "w") as f:
        f.write(header)
    print(f"-> {args.output} ({len(frames)} frames, {PANEL_W}x{PANEL_H}, kaomojis + strobe)")


if __name__ == "__main__":
    main()
