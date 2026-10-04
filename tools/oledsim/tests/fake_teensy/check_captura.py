#!/usr/bin/env python3
"""check_captura.py <captura_dir> <N> <sim_out_dir> <sim_state>

Cada frame que captura.py --gesto <N> sacó del fake_teensy (gesto<N>_*.txt, el dump 'O' del
firmware) tiene que ser un frame que el simulador también pinta para ese preset (mismos
pixeles), y los momentos clave del simulador (1er boop agarrado, barrita con palomita si hubo
FIRE) tienen que aparecer en la captura."""
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
import oledlook  # noqa: E402


def captured(d, n):
    out = []
    for p in sorted(glob.glob(os.path.join(d, f"gesto{n}_*.txt"))):
        pages = {}
        for ln in open(p):
            if ln.startswith("L0"):
                k, h = ln.split()
                pages[int(k[1:])] = bytes.fromhex(h)
        px = tuple(tuple((pages[y >> 3][x] >> (y & 7)) & 1 for x in range(128)) for y in range(64))
        out.append((os.path.basename(p), px))
    return out


def main():
    cdir, n, sdir, state = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    _, frames = oledlook.parse_frames(os.path.join(sdir, "raw", state + ".frames"))
    ix = json.load(open(os.path.join(sdir, "index.json")))
    boop = next(s["boop"] for s in ix["states"] if s["state"] == state)
    ev = {}
    for e in boop["events"]:
        ev.setdefault(e["name"], e["t_ms"])
    key = lambda f: tuple(tuple(int(v) for v in row) for row in f["px"])
    sim = {key(f) for f in frames}
    want = {}
    if "tap1" in ev and "hold" in ev:   # mano + 1 puntito
        want["1er boop"] = {key(f) for f in frames if ev["tap1"] < f["tx"] <= ev["hold"]}
    if "FIRE" in ev:                     # barrita con palomita
        want["palomita"] = {key(f) for f in frames if ev["FIRE"] < f["tx"] <= ev["FIRE"] + 500}
    cap = captured(cdir, n)
    bad = [name for name, px in cap if px not in sim]
    ok = len(cap) >= 3 and not bad
    print(f"{len(cap)} frames capturados, {len(cap) - len(bad)} idénticos a uno del simulador ({state})")
    for name in bad:
        print("  NO está en el simulador:", name)
    for what, pics in want.items():
        hit = any(px in pics for _, px in cap)
        print(f"  {what}: {'sí' if hit else 'NO'} salió en la captura")
        ok = ok and hit
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
