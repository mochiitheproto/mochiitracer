#!/usr/bin/env python3
"""Contact sheets.

    sheet.py <out_dir> [-o sheet.png] [--cols 4] [--scale 3] [--title T]
        labelled grid of every image of one render (static _a/_b pairs collapse to one tile)
    sheet.py --compare <out_dirA> <out_dirB> ... [-o compare.png] [--labels A B ...] [--scale 2]
        one row per state, one column per design (side by side)
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oledlook  # noqa: E402

BG = (18, 20, 24)
FG = (230, 232, 236)
DIM = (140, 146, 156)
ACCENT = (255, 190, 90)
SKIP_SPEC = {"name", "phases", "gif", "gif_ms", "phase_window", "record", "events",
             "boop_at", "strip", "strip_cols", "fire_mask", "render_ms"}


def fit(d, text, fnt, maxw):
    if d.textlength(text, font=fnt) <= maxw:
        return text
    while text and d.textlength(text + "…", font=fnt) > maxw:
        text = text[:-1]
    return text + "…"


def load_index(out_dir):
    with open(os.path.join(out_dir, "index.json")) as f:
        return json.load(f)


def px_of(out_dir, entry):
    im = np.asarray(Image.open(os.path.join(out_dir, entry["file_1x"])).convert("RGB")).astype(int)
    return (im.max(axis=2) > 40).astype(np.uint8)


def tile(out_dir, entry, scale, tint):
    return oledlook.image_oled(px_of(out_dir, entry), scale=scale, contrast=entry["contrast"],
                               on=entry["on"], tint=tint, margin=scale * 2)


def spec_text(entry):
    sp = entry.get("spec", {})
    parts = []
    for k, v in sp.items():
        if k in SKIP_SPEC:
            continue
        if k == "values" and isinstance(v, dict):
            parts += [f"{a}={b}" for a, b in v.items()]
        elif k == "mask":
            parts.append("mask=" + (os.path.basename(v) if isinstance(v, str) else str(v)))
        elif k == "boop":
            parts.append("boop=" + (v if isinstance(v, str) else f"{len(v)} edges"))
        else:
            parts.append(f"{k}={v}")
    if sp.get("events"):
        parts.append(f"+{len(sp['events'])} event(s)")
    parts.append(f"t={entry['t_ms'] / 1000:.2f}s")
    return "  ".join(parts)


def group_entries(entries):
    """Collapse static _a/_b pairs into one item; returns list of (label, entry, note)."""
    items, seen = [], set()
    for e in entries:
        if e["name"] in seen:
            continue
        if e.get("phase") == "a" and e.get("static"):
            items.append((e["state"], e, "static (a = b)"))
            seen.add(e["state"] + "_b")
        else:
            note = ""
            if e.get("phase"):
                note = "phase " + e["phase"] + (" (blink/anim)" if not e.get("static") else "")
            items.append((e["name"], e, note))
        seen.add(e["name"])
    return items


def contact_sheet(out_dir, path, title=None, tint=None, cols=4, scale=3):
    idx = load_index(out_dir)
    tint = tint or idx.get("tint", "white")
    items = group_entries(idx["images"])
    if not items:
        return None
    tiles = [tile(out_dir, e, scale, tint) for _, e, _ in items]
    tw, th = tiles[0].size
    pad, lab = 14, 46
    cols = max(1, min(cols, len(tiles)))
    rows = (len(tiles) + cols - 1) // cols
    head = 52
    W = cols * (tw + pad) + pad
    H = head + rows * (th + lab + pad) + pad
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.text((pad, 12), title or idx.get("design", ""), fill=FG, font=oledlook.font(18, True))
    d.text((pad, 34), os.path.basename(idx.get("scenario", "")) + f"  ·  tint {tint}  ·  {len(idx['images'])} images",
           fill=DIM, font=oledlook.font(12))
    for i, ((label, e, note), t) in enumerate(zip(items, tiles)):
        r, c = divmod(i, cols)
        x = pad + c * (tw + pad)
        y = head + r * (th + lab + pad)
        img.paste(t, (x, y))
        d.text((x + 2, y + th + 4), fit(d, label, oledlook.font(14, True), tw - 4), fill=FG, font=oledlook.font(14, True))
        sub = spec_text(e)
        if note:
            sub = note + "  ·  " + sub
        d.text((x + 2, y + th + 23), fit(d, sub, oledlook.font(11), tw - 4), fill=ACCENT if "blink" in note else DIM, font=oledlook.font(11))
        if e.get("warnings"):
            d.text((x + 2, y + th + 35), fit(d, "! " + e["warnings"], oledlook.font(10), tw - 4), fill=(255, 110, 110), font=oledlook.font(10))
    img.save(path)
    return path


def compare_sheet(dirs, path, labels=None, scale=2):
    idxs = [load_index(d) for d in dirs]
    labels = labels or [os.path.basename(os.path.normpath(d)) for d in dirs]
    order = []
    for ix in idxs:
        for e in ix["images"]:
            if e["name"] not in order:
                order.append(e["name"])
    by = [{e["name"]: e for e in ix["images"]} for ix in idxs]
    tw = 128 * scale + 4 * scale
    th = 64 * scale + 4 * scale
    pad, labw, head = 10, 190, 40
    W = labw + len(dirs) * (tw + pad) + pad
    H = head + len(order) * (th + pad) + pad
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    for j, l in enumerate(labels):
        d.text((labw + j * (tw + pad), 12), l, fill=FG, font=oledlook.font(15, True))
    for i, name in enumerate(order):
        y = head + i * (th + pad)
        d.text((pad, y + th // 2 - 8), fit(d, name, oledlook.font(12, True), labw - 2 * pad), fill=FG, font=oledlook.font(12, True))
        for j, (dd, b, ix) in enumerate(zip(dirs, by, idxs)):
            e = b.get(name)
            x = labw + j * (tw + pad)
            if e is None:
                d.text((x + 8, y + th // 2 - 6), "(missing)", fill=DIM, font=oledlook.font(11))
                continue
            img.paste(tile(dd, e, scale, ix.get("tint", "white")), (x, y))
    img.save(path)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("-o", "--output", default=None)
    ap.add_argument("--compare", action="store_true")
    ap.add_argument("--labels", nargs="*", default=None)
    ap.add_argument("--cols", type=int, default=4)
    ap.add_argument("--scale", type=int, default=None)
    ap.add_argument("--title", default=None)
    a = ap.parse_args()
    if a.compare:
        out = a.output or "compare.png"
        print(compare_sheet(a.dirs, out, a.labels, a.scale or 2))
    else:
        for dd in a.dirs:
            out = a.output or os.path.join(dd, "contact_sheet.png")
            print(contact_sheet(dd, out, a.title, None, a.cols, a.scale or 3))


if __name__ == "__main__":
    main()
