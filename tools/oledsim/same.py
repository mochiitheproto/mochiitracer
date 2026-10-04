#!/usr/bin/env python3
"""Regression check between two render outputs (same scenario, two designs or two sim versions).

    same.py <dirA> <dirB> [--strict] [--only SUBSTR]

* Every *_1x.png (the exact 128x64 picture, brightness from the contrast included) is compared
  pixel by pixel. Images missing on one side count as differences.
* When both dirs have raw/*.frames, the whole recorded sequence of each state (every distinct
  frame the GIF is made of: pixels, contrast, display on/off) is compared too.
* --strict also requires the same frame times (snapshot + end of display()) and, when both
  sides have raw/*.boop, the same OLED transfers (time, bytes): i.e. identical bus traffic.

Exit 0 when everything compared is identical, 1 otherwise.
"""
import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oledlook  # noqa: E402


def png_diff(a, b):
    ia = np.asarray(Image.open(a).convert("RGB"))
    ib = np.asarray(Image.open(b).convert("RGB"))
    if ia.shape != ib.shape:
        return f"size {ia.shape[1]}x{ia.shape[0]} vs {ib.shape[1]}x{ib.shape[0]}"
    n = int(np.any(ia != ib, axis=2).sum())
    return f"{n} px differ" if n else None


def seq_diff(fa, fb, strict):
    _, A = oledlook.parse_frames(fa)
    _, B = oledlook.parse_frames(fb)
    for i, (x, y) in enumerate(zip(A, B)):
        if not np.array_equal(x["px"], y["px"]):
            return f"frame {i} (t={x['t']:.0f} vs {y['t']:.0f} ms): {int((x['px'] != y['px']).sum())} px differ"
        if x["contrast"] != y["contrast"] or x["on"] != y["on"]:
            return f"frame {i} (t={x['t']:.0f} ms): contrast/on 0x{x['contrast']:02X}/{x['on']} vs 0x{y['contrast']:02X}/{y['on']}"
        if strict and (abs(x["t"] - y["t"]) > 1e-6 or abs(x["shown"] - y["shown"]) > 1e-6):
            return f"frame {i}: time {x['t']:.3f}/{x['shown']:.3f} vs {y['t']:.3f}/{y['shown']:.3f} ms"
    if len(A) != len(B):
        return f"{len(A)} vs {len(B)} distinct frames"
    return None


def xfers(path):
    out = []
    with open(path) as f:
        for line in f:
            if line.startswith("X "):
                out.append(tuple(line.split()[1:6]))
    return out


def bus_diff(fa, fb):
    A, B = xfers(fa), xfers(fb)
    for i, (x, y) in enumerate(zip(A, B)):
        if x != y:
            return f"transfer {i}: t={x[0]} bus={x[1]} data={x[2]} cmd={x[3]}  vs  t={y[0]} bus={y[1]} data={y[2]} cmd={y[3]}"
    if len(A) != len(B):
        return f"{len(A)} vs {len(B)} OLED transfers"
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir_a")
    ap.add_argument("dir_b")
    ap.add_argument("--strict", action="store_true", help="also frame times and OLED bus traffic")
    ap.add_argument("--only", default=None)
    a = ap.parse_args()

    bad, checked = [], 0
    na = {os.path.basename(p) for p in glob.glob(os.path.join(a.dir_a, "*_1x.png"))}
    nb = {os.path.basename(p) for p in glob.glob(os.path.join(a.dir_b, "*_1x.png"))}
    for n in sorted(na | nb):
        if a.only and a.only not in n:
            continue
        img = n[:-len("_1x.png")]
        if n not in nb:
            bad.append((img, "only in A"))
            continue
        if n not in na:
            bad.append((img, "only in B"))
            continue
        checked += 1
        d = png_diff(os.path.join(a.dir_a, n), os.path.join(a.dir_b, n))
        if d:
            bad.append((img, d))

    seqs = 0
    ra, rb = os.path.join(a.dir_a, "raw"), os.path.join(a.dir_b, "raw")
    if os.path.isdir(ra) and os.path.isdir(rb):
        fa = {os.path.basename(p) for p in glob.glob(os.path.join(ra, "*.frames"))}
        fb = {os.path.basename(p) for p in glob.glob(os.path.join(rb, "*.frames"))}
        for n in sorted(fa | fb):
            st = n[:-len(".frames")]
            if a.only and a.only not in st:
                continue
            if n not in fa or n not in fb:
                bad.append((st + " [sequence]", "only in " + ("B" if n not in fa else "A")))
                continue
            seqs += 1
            d = seq_diff(os.path.join(ra, n), os.path.join(rb, n), a.strict)
            if d:
                bad.append((st + " [sequence]", d))
            ba, bb = os.path.join(ra, st + ".boop"), os.path.join(rb, st + ".boop")
            if a.strict and os.path.exists(ba) and os.path.exists(bb):
                d = bus_diff(ba, bb)
                if d:
                    bad.append((st + " [bus]", d))

    for name, why in bad:
        print(f"DIFF  {name}: {why}")
    print(f"{checked} images, {seqs} frame sequences compared{' (strict)' if a.strict else ''}: "
          + ("all identical" if not bad else f"{len(bad)} difference(s)"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
