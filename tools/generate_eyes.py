#!/usr/bin/env python3
"""
Genera un slot de OJOS pop-art (bullseyes neón) animados: dos ojos concéntricos
que miran a todos lados y ciclan colores entre la MISMA paleta sacada de una
imagen de referencia. >= 60 frames, loop suave.

colors = 3*n_colors - 2 al Image() (fix bug GetRGB). n_colors <= 85.

Usage:
  generate_eyes.py ref.jpeg out.h --class-name Eyes --frames 72 --preview-dir /tmp/p
"""
import argparse, colorsys, math
import numpy as np
from PIL import Image, ImageDraw

PANEL_W, PANEL_H = 64, 32
SS = 4                                   # supersample para circulos suaves


# ---------- PALETA desde la referencia ----------

def extract_palette(path, k=32):
    src = Image.open(path).convert("RGB").resize((200, 200))
    pal_img = src.convert("P", palette=Image.ADAPTIVE, colors=k)
    pal = pal_img.getpalette()[: k * 3]
    cols = [(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]) for i in range(k)]
    # bg = el mas oscuro; neons = brillantes y saturados
    def lum(c): return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
    def sat(c):
        r, g, b = [x / 255 for x in c]
        return colorsys.rgb_to_hsv(r, g, b)[1]
    bg = min(cols, key=lum)
    neons = [c for c in cols if lum(c) > 95 and sat(c) > 0.5]
    # ordena por matiz para que el ciclo se vea como arcoiris fluido
    neons.sort(key=lambda c: colorsys.rgb_to_hsv(*[x / 255 for x in c])[0])
    # dedup cercanos
    uniq = []
    for c in neons:
        if all(abs(c[0] - u[0]) + abs(c[1] - u[1]) + abs(c[2] - u[2]) > 60 for u in uniq):
            uniq.append(c)
    return bg, uniq


# ---------- GAZE PATH (mira a todos lados) ----------

def gaze_path(n_frames):
    # 8 direcciones + centro, recorrido suave tipo "scan" alrededor
    dirs = [(0, 0), (1, 0), (1, -1), (0, -1), (-1, -1), (-1, 0),
            (-1, 1), (0, 1), (1, 1), (0, 0)]
    pts = []
    seg = n_frames / (len(dirs) - 1)
    for f in range(n_frames):
        t = f / seg
        i = min(int(t), len(dirs) - 2)
        frac = t - i
        # ease in-out
        e = 0.5 - 0.5 * math.cos(frac * math.pi)
        ax, ay = dirs[i]; bx, by = dirs[i + 1]
        pts.append((ax + (bx - ax) * e, ay + (by - ay) * e))
    return pts


# ---------- DIBUJO ----------

def draw_eye(d, cx, cy, gaze, ring_cols, bg):
    """Bullseye: aro externo + aro medio + iris (que se mueve) + pupila."""
    gx, gy = gaze
    cA, cB, cC = ring_cols
    R = 12 * SS
    cx, cy = cx * SS, cy * SS
    # aro externo
    d.ellipse([cx - R, cy - R, cx + R, cy + R], fill=cA)
    # aro medio
    r2 = 9 * SS
    d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=cB)
    # iris (se desplaza segun gaze)
    off = 4 * SS
    ix, iy = cx + int(gx * off), cy + int(gy * off)
    r3 = 5.5 * SS
    d.ellipse([ix - r3, iy - r3, ix + r3, iy + r3], fill=cC)
    # pupila oscura
    rp = 2.5 * SS
    d.ellipse([ix - rp, iy - rp, ix + rp, iy + rp], fill=bg)
    # brillo (catchlight)
    rb = 1.0 * SS
    d.ellipse([ix - r3 * 0.4 - rb, iy - r3 * 0.4 - rb,
               ix - r3 * 0.4 + rb, iy - r3 * 0.4 + rb], fill=(255, 255, 255))


def accent_shards(d, neons, phase):
    """Triangulitos neón tenues en las esquinas pa' el vibe pop-art."""
    tris = [[(0, 0), (16, 0), (0, 14)],
            [(64, 4), (64, 22), (52, 12)],
            [(2, 32), (14, 32), (8, 22)],
            [(46, 30), (62, 30), (56, 20)]]
    for i, t in enumerate(tris):
        c = neons[(phase + i) % len(neons)]
        c = tuple(int(x * 0.32) for x in c)        # tenue
        d.polygon([(x * SS, y * SS) for x, y in t], fill=c)


def build_frames(bg, neons, n_frames):
    gz = gaze_path(n_frames)
    frames = []
    nn = len(neons)
    for f in range(n_frames):
        big = Image.new("RGB", (PANEL_W * SS, PANEL_H * SS), bg)
        d = ImageDraw.Draw(big)
        # fondo: navy un pelin mas claro que la pupila + shards tenues
        cphase = f * nn // n_frames
        accent_shards(d, neons, cphase)
        # color cycle: cada ojo con fase distinta
        kL = (f // 3)
        kR = (f // 3) + 1
        ringsL = (neons[kL % nn], neons[(kL + 2) % nn], neons[(kL + 4) % nn])
        ringsR = (neons[(kR + 1) % nn], neons[(kR + 3) % nn], neons[(kR + 5) % nn])
        draw_eye(d, 17, 16, gz[f], ringsL, bg)
        draw_eye(d, 47, 16, gz[f], ringsR, bg)
        small = big.resize((PANEL_W, PANEL_H), Image.LANCZOS)
        frames.append(small)
    return frames


# ---------- ENCODE (igual que generate_video) ----------

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
    import os
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
        duration=80, loop=0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("output")
    ap.add_argument("--class-name", required=True)
    ap.add_argument("--frames", type=int, default=72)
    ap.add_argument("--n-colors", type=int, default=48)
    ap.add_argument("--preview-dir", default=None)
    args = ap.parse_args()

    bg, neons = extract_palette(args.ref)
    print("bg:", bg, "| neons:", neons)
    if len(neons) < 4:
        neons = neons + [(255, 240, 40), (80, 230, 90), (70, 210, 230),
                         (235, 60, 150), (160, 80, 220)]
    frames = build_frames(bg, neons, args.frames)
    if args.preview_dir:
        save_preview(frames, args.preview_dir, args.class_name)
    indexed, palette = quantize_shared(frames, args.n_colors)
    hdr = emit_header(args.class_name, PANEL_W, PANEL_H, palette, indexed)
    with open(args.output, "w") as f:
        f.write(hdr)
    print(f"-> {args.output} | {len(frames)} frames, n_colors={args.n_colors}, "
          f"~{len(frames)*PANEL_W*PANEL_H} data bytes")


if __name__ == "__main__":
    main()
