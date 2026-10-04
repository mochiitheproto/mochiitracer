#!/usr/bin/env python3
"""Peripheral-vision check: can a picture be told apart at a glance through the helmet?

  ./glance.py <render_out_dir> <boop_state> [--at=-100,+300,...] [-o glance.png]

For a boop state of a render (raw/<state>.frames + index.json), it takes the frame visible at each
time (ms relative to boop_at; default: just before the boop, right after every gesture event,
and the last frame) and draws each one three ways: exact 1x, blurred 1 px and blurred 2 px
(brightness follows the panel contrast). It also prints how much each picture differs from the
previous one under the 2 px blur (mean |diff| x100): what is left of the change in peripheral
vision. 1 px details vanish under the blur, so a step that only changes those scores near 0.
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import oledlook  # noqa: E402


def gray(fr):
    """frame -> float image 0..1, lit pixels at the contrast's brightness"""
    return fr["px"].astype(np.float32) * oledlook.brightness(fr["contrast"], fr["on"])


def blur(a, r):
    if not r:
        return a
    im = Image.fromarray((a * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r))
    return np.asarray(im, dtype=np.float32) / 255.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_dir")
    ap.add_argument("state")
    ap.add_argument("--at", default=None, help="comma separated ms relative to boop_at (--at=-100,+300)")
    ap.add_argument("-o", "--output", default=None)
    ap.add_argument("--scale", type=int, default=3)
    a = ap.parse_args()

    ix = json.load(open(os.path.join(a.out_dir, "index.json")))
    st = next((s for s in ix["states"] if s["state"] == a.state), None)
    if not st or not st.get("boop"):
        raise SystemExit(f"{a.state}: not a boop state of {a.out_dir}")
    b = st["boop"]
    at0 = b["boop_at"]
    _, frames = oledlook.parse_frames(os.path.join(a.out_dir, "raw", a.state + ".frames"))

    if a.at:
        rels = [int(x) for x in a.at.split(",")]
    else:  # the picture each event leads to: the first frame after it, plus before/after
        rels = [-100] + [int(e["rel_ms"]) + 1 for e in b["events"]]
        rels.append(int(frames[-1]["tx"] - at0))
    picks = []
    for r in rels:
        t = at0 + r
        if not a.at:  # an event: the first picture that appears after it
            later = [f for f in frames if f["tx"] >= t]
            fr = later[0] if later and r != rels[0] else None
        else:
            fr = None
        if fr is None:
            seen = [f for f in frames if f["tx"] <= t]
            fr = seen[-1] if seen else frames[0]
        picks.append((r, fr))

    s, gap, lab = a.scale, 6, 18
    tw, th = 128 * s, 64 * s
    img = Image.new("RGB", (len(picks) * (tw + gap) + gap, 3 * (th + gap) + lab + gap), (18, 20, 24))
    d = ImageDraw.Draw(img)
    f = oledlook.font(13)
    prev = None
    print(f"{a.state}: blur-2 difference from the previous picture (mean |diff| x100)")
    for i, (r, fr) in enumerate(picks):
        g = gray(fr)
        x = gap + i * (tw + gap)
        for j, rad in enumerate((0, 1.0, 2.0)):
            a2 = blur(np.kron(g, np.ones((4, 4), np.float32)), rad * 4)[::4, ::4] if rad else g
            tile = Image.fromarray((np.clip(a2, 0, 1) * 255).astype(np.uint8)).resize((tw, th), Image.NEAREST if not rad else Image.BILINEAR)
            img.paste(tile.convert("RGB"), (x, gap + j * (th + gap)))
        b2 = blur(np.kron(g, np.ones((4, 4), np.float32)), 8)
        score = "" if prev is None else f"{100 * float(np.abs(b2 - prev).mean()):.1f}"
        prev = b2
        tx = int(fr["tx"] - at0)
        d.text((x + 2, gap + 3 * (th + gap)), f"{r:+d} (frame {tx:+d})  {score}", fill=(230, 230, 230), font=f)
        print(f"  {r:+6d} ms  frame {tx:+6d}  contrast 0x{fr['contrast']:02X}  {score or '-'}")
    out = a.output or os.path.join(a.out_dir, f"{a.state}_glance.png")
    img.save(out)
    print(out)


if __name__ == "__main__":
    main()
