"""Shared helpers: parse .frames files, render the physical OLED look, fonts."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 128, 64

TINTS = {
    # lit color of the panel (RGB); "yb" = two-colour module, top 16 rows yellow
    "white": (222, 238, 255),
    "blue": (96, 186, 255),
    "yb": None,
}
OFF = (7, 9, 12)          # unlit pixel / panel glass
BEZEL = (24, 27, 32)      # thin outline around the active area


def parse_frames(path):
    """Return (meta, frames). frames = list of dict(t, shown, contrast, on, invert, tx, px=np.uint8[64,128]).
    t = snapshot time (end of the loop iteration), shown = end of the last display(),
    tx = end of the last transmission to the panel (data or command: when this picture appeared)."""
    meta, frames, cur = {}, [], None
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("# "):
                parts = line[2:].split(" ", 1)
                meta[parts[0]] = parts[1] if len(parts) > 1 else ""
            elif line.startswith("@ "):
                p = line[2:].split()
                t, shown, contrast, on, inv = p[:5]
                cur = {"t": float(t), "shown": float(shown), "contrast": int(contrast),
                       "on": on == "1", "invert": inv == "1", "tx": float(p[5]) if len(p) > 5 else float(shown),
                       "rows": []}
                frames.append(cur)
            elif cur is not None and len(line) == W:
                cur["rows"].append(line)
    for fr in frames:
        fr["px"] = np.array([[c == "1" for c in r] for r in fr.pop("rows")], dtype=np.uint8)
    return meta, frames


def brightness(contrast, on=True):
    if not on:
        return 0.0
    # SSD1306 at contrast 0 is still clearly visible; 0xCF (Adafruit default) = full.
    return float(np.clip(0.45 + 0.55 * contrast / 207.0, 0.45, 1.0))


def lit_colors(tint):
    """(64,3) array: lit colour per row."""
    if tint == "yb":
        col = np.zeros((H, 3), np.float32)
        col[:16] = (255, 214, 64)
        col[16:] = (96, 186, 255)
        return col
    return np.tile(np.array(TINTS.get(tint) or TINTS["white"], np.float32), (H, 1))


def image_1x(px, contrast=0xCF, on=True, tint="white"):
    """Exact 128x64 image: lit pixels in panel colour, unlit = OFF."""
    b = brightness(contrast, on)
    col = lit_colors(tint) * b
    off = np.array(OFF, np.float32)
    img = np.where(px[:, :, None].astype(bool), col[:, None, :], off[None, None, :])
    return Image.fromarray(img.astype(np.uint8), "RGB")


def image_oled(px, scale=6, contrast=0xCF, on=True, tint="white", glow=True, margin=None):
    """Physical look: each pixel a (scale-1)^2 dot with a 1 px gap, soft bloom, bezel."""
    b = brightness(contrast, on)
    gap = 1 if scale >= 4 else 0
    dot = scale - gap
    m = scale * 2 if margin is None else margin
    cw, ch = W * scale + 2 * m, H * scale + 2 * m
    lit = np.zeros((ch, cw), np.float32)
    mask = np.zeros((H * scale, W * scale), np.float32)
    cell = np.zeros((scale, scale), np.float32)
    cell[:dot, :dot] = 1.0
    if dot >= 4:  # round the dot corners a hair
        cell[0, 0] = cell[0, dot - 1] = cell[dot - 1, 0] = cell[dot - 1, dot - 1] = 0.55
    mask[:] = np.kron(px.astype(np.float32), cell)
    lit[m:m + H * scale, m:m + W * scale] = mask
    rowcol = lit_colors(tint) * b                       # (64,3)
    rowmap = np.repeat(rowcol, scale, axis=0)           # (64*scale,3)
    colmap = np.zeros((ch, 3), np.float32)
    colmap[m:m + H * scale] = rowmap
    colmap[:m] = rowmap[0]
    colmap[m + H * scale:] = rowmap[-1]
    base = np.array(OFF, np.float32)[None, None, :]
    img = base + lit[:, :, None] * (colmap[:, None, :] - base)
    if glow and b > 0:
        g = Image.fromarray((lit * 255).astype(np.uint8), "L").filter(ImageFilter.GaussianBlur(scale * 0.9))
        g = np.asarray(g, np.float32)[:, :, None] / 255.0
        img = img + 0.30 * g * colmap[:, None, :] * (1 - lit[:, :, None])
    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(out)
    d.rectangle([m - gap - 2, m - gap - 2, m + W * scale + 1, m + H * scale + 1], outline=BEZEL, width=1)
    return out


_FONT_CACHE = {}


def font(size, bold=False):
    key = (size, bold)
    if key not in _FONT_CACHE:
        names = ["DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"]
        f = None
        for n in names:
            for d in ("/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/TTF", "/usr/share/fonts/dejavu"):
                p = os.path.join(d, n)
                if os.path.exists(p):
                    f = ImageFont.truetype(p, size)
                    break
            if f:
                break
        _FONT_CACHE[key] = f or ImageFont.load_default()
    return _FONT_CACHE[key]
