#!/usr/bin/env python3
"""Render a HUD design (a Displays/ dir with SSD1306.h/.cpp) for a scenario.

    render.py <displays_dir> [scenario.json] <out_dir> [--scale 6] [--tint white|blue|yb]
              [--only SUBSTR] [--no-sheet] [--title TEXT] [--lang es|en|zh]

Builds the simulator for the design (build.sh), turns the scenario into a plan,
runs every state in a fresh process, and writes:
    out/<state>.png        what you physically SEE, 6x, OLED look
    out/<state>_1x.png     exact 128x64 (lit = panel colour, unlit = near-black)
    out/<state>_a/_b.*     for states with "phases": the frame at t and the next different one
    out/<state>.gif        for states with "gif": every change in the recorded window
    out/<state>_strip.png  for boop states with "strip": every distinct frame around the gesture
    out/index.json         metadata per image (times, contrast, static?, warnings) + per-state
                           boop gesture log (events, OLED transfers, frame gaps, bus budget)
    out/contact_sheet.png  labelled grid of everything
    out/raw/*.frames       raw 0/1 frames from the simulator
    out/raw/*.boop         raw gesture/loop/bus log from the simulator
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import oledlook  # noqa: E402
import sheet  # noqa: E402
import boopstrip  # noqa: E402
from faces import CAPTURED_MASKS, FACES, REFERENCE_MASKS  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(HERE), "oled"))
from textos import LANGS  # noqa: E402  (the HUD's language numbers: es, en, zh)

MENUS = ["Faces", "Bright", "AccentBright", "Microphone", "MicLevel", "BoopSensor",
         "SpectrumMirror", "FaceSize", "Color", "HueF", "HueB", "EffectS", "FanSpeed"]
# faces with a captured LED mask in masks/ (what EnableBitFaceRender sees), by face number
AUTO_MASKS = {FACES.index(n): m for n, m in CAPTURED_MASKS.items()}
MENU_MAX = {m: (2 if m in ("Microphone", "BoopSensor", "SpectrumMirror") else 10) for m in MENUS}
MENU_MAX["Faces"] = len(FACES)
# mask loaded when a boop FIRE (Menu::NextFace) lands on a face, for states that follow the face
# (mask "auto", or "fire_mask": "follow"): the captures + the reference renders of 1-5 and 10
FIRE_MASKS = {**{f: os.path.join(HERE, "masks", m) for f, m in AUTO_MASKS.items()},
              **{FACES.index(n): os.path.join(HERE, "scenarios", m) for n, m in REFERENCE_MASKS.items()}}
# the boop presets live in ONE table, shared with the firmware injection tooling
BOOP_PRESETS = os.path.join(HERE, "boop_presets.json")
BOOP_RENDER_MS = 12.0   # head: ~12 ms per frame without an OLED transfer (~83 FPS)
STRIP_PRE_MS = 200      # strips start this long before the boop


def menu_index(v):
    if isinstance(v, int):
        return v
    for i, m in enumerate(MENUS):
        if m.lower() == str(v).lower():
            return i
    raise SystemExit(f"unknown menu '{v}' (use 0-12 or one of {MENUS})")


def face_index(v):
    if isinstance(v, int):
        return v
    s = str(v).upper()
    if s in FACES:
        return FACES.index(s)
    raise SystemExit(f"unknown face '{v}'")


def lang_index(v):
    if isinstance(v, int) and 0 <= v < len(LANGS):
        return v
    if str(v).lower() in LANGS:
        return LANGS.index(str(v).lower())
    raise SystemExit(f"unknown lang '{v}' (use {', '.join(LANGS)} or 0..{len(LANGS) - 1})")


def resolve_mask(v, face, scen_dir):
    if v is None or v == "auto":
        f = AUTO_MASKS.get(face)
        return os.path.join(HERE, "masks", f) if f else "none"
    if v in ("none", "", False):
        return "none"
    for base in (scen_dir, os.path.join(HERE, "masks"), "", HERE):
        p = os.path.join(base, v) if base else v
        if os.path.isfile(p):
            return os.path.abspath(p)
    raise SystemExit(f"mask not found: {v}")


def sets_for(fields, scen_dir, face_for_auto):
    """Turn a dict of state fields into plan 'set' lines (order matters: face before mask)."""
    out = []
    face = face_for_auto
    if "menu" in fields:
        out.append(f"set menu {menu_index(fields['menu'])}")
    if "face" in fields:
        face = face_index(fields["face"])
        out.append(f"set face {face}")
    vals = fields.get("values", {})
    if isinstance(vals, list):
        vals = {MENUS[i]: v for i, v in enumerate(vals)}
    for k in MENUS:   # shorthand: "Bright": 3 directly in the state
        if k in fields:
            vals = dict(vals, **{k: fields[k]})
    for k, v in vals.items():
        i = menu_index(k)
        if i == 0:
            face = face_index(v)
            out.append(f"set face {face}")
        else:
            if not (0 <= int(v) < MENU_MAX[MENUS[i]]):
                print(f"warning: {MENUS[i]}={v} outside the real menu range 0..{MENU_MAX[MENUS[i]] - 1}", file=sys.stderr)
            out.append(f"set val {i} {int(v)}")
    if "temp" in fields:
        out.append(f"set temp {float(fields['temp'])}")
    if "booped" in fields:
        out.append(f"set booped {1 if fields['booped'] else 0}")
    if "fps" in fields:
        out.append(f"set fps {float(fields['fps'])}")
    if "lang" in fields:
        out.append(f"set lang {lang_index(fields['lang'])}")
    if "mask" in fields or "face" in fields or any(menu_index(k) == 0 for k in vals):
        m = fields.get("mask", "auto")
        out.append(f"set mask {resolve_mask(m, face, scen_dir)}")
        fm = fields.get("fire_mask", "auto")
        follow = fm == "follow" or (fm == "auto" and m in (None, "auto"))
        out.append(f"set maskfollow {1 if follow else 0}")
    return out, face


def load_presets():
    with open(BOOP_PRESETS) as f:
        return json.load(f)["presets"]


def boop_timeline(full, millis):
    """Sensor edges [[t_ms, 0|1], ...] for a state's "boop" field, and the boop start (ms)."""
    b = full.get("boop")
    if b is None or b is False:
        return None, None
    if isinstance(b, str):
        presets = load_presets()
        if b not in presets:
            raise SystemExit(f"unknown boop preset '{b}' (have: {', '.join(presets)})")
        at = int(full.get("boop_at", millis - 500))
        t, v, edges = at, 1, []
        for d in presets[b]["pattern"]:
            edges.append([t, v])
            t += int(d)
            v ^= 1
        if v == 0:            # ended on an ON segment: let go
            edges.append([t, 0])
        return edges, at
    edges = sorted([[int(t), 1 if v else 0] for t, v in b], key=lambda e: e[0])
    ons = [t for t, v in edges if v]
    at = int(full.get("boop_at", ons[0] if ons else (edges[0][0] if edges else millis)))
    return edges, at


def load_scenario(path):
    with open(path) as f:
        sc = json.load(f)
    if isinstance(sc, list):
        sc = {"states": sc}
    return sc


def make_plan(sc, scen_dir, only=None, lang=None):
    d = dict(sc.get("defaults", {}))
    if lang is not None:   # --lang: for the states that do not pick their own
        d["lang"] = lang
    lines = [f"init_ms {int(sc.get('init_ms', 0))}",
             f"loop_hz {float(sc.get('loop_hz', 120))}",
             f"render_ms {float(sc.get('render_ms', 4))}"]
    states = []
    for st in sc["states"]:
        if only and only not in st["name"]:
            continue
        full = dict(d)
        full["values"] = dict(d.get("values", {}))
        for k, v in st.items():
            if k == "values":
                full["values"].update(v if isinstance(v, dict) else {MENUS[i]: x for i, x in enumerate(v)})
            else:
                full[k] = v
        full.setdefault("menu", 0)
        full.setdefault("face", 0)
        full.setdefault("mask", "auto")
        millis = int(full.get("millis", 10000))
        phases = bool(full.get("phases", False))
        gif = bool(full.get("gif", False))
        record = int(full.get("record", 0))
        if phases:
            record = max(record, int(full.get("phase_window", 1500)))
        edges, boop_at = boop_timeline(full, millis)
        if gif and edges is None:
            record = max(record, int(full.get("gif_ms", 2000)))
        strip = bool(full.get("strip", False)) and edges is not None
        rec_from = None
        if edges is not None and (gif or strip):
            # gif/strip of a boop state: [boop_at - 200, boop_at + gif_ms]
            rec_from = boop_at - STRIP_PRE_MS
            record = max(record, boop_at + int(full.get("gif_ms", 2000)) - millis)
        lines += [f"state {st['name']}", f"millis {millis}", f"record {record}"]
        if rec_from is not None and rec_from < millis:
            lines.append(f"rec_from {max(0, rec_from)}")
        render_ms = full.get("render_ms", BOOP_RENDER_MS if edges is not None else None)
        if render_ms is not None:
            lines.append(f"state_render_ms {float(render_ms)}")
        for t, v in edges or []:
            lines.append(f"boop {t} {v}")
        s0, face = sets_for(full, scen_dir, 0)
        lines += s0
        for ev in sorted(full.get("events", []), key=lambda e: e["t"]):
            lines.append(f"event {int(ev['t'])}")
            evs, face = sets_for(ev, scen_dir, face)
            lines += evs
        states.append({"name": st["name"], "millis": millis, "phases": phases, "gif": gif,
                       "record": record, "spec": {k: v for k, v in st.items()},
                       "boop": None if edges is None else {
                           "preset": full["boop"] if isinstance(full["boop"], str) else None,
                           "boop_at": boop_at, "edges": edges, "strip": strip,
                           "strip_from": boop_at - STRIP_PRE_MS,
                           "strip_to": boop_at + int(full.get("gif_ms", 2000)),
                           "strip_cols": int(full.get("strip_cols", 4)),
                           # Menu BoopSensor at t=0 (stand-in default 1): off = no gesture expected
                           "sensor_on": int(full.get("BoopSensor", full["values"].get("BoopSensor", 1))) != 0}})
    lines[3:3] = [f"firemask {f} {m}" for f, m in sorted(FIRE_MASKS.items()) if os.path.isfile(m)]
    return "\n".join(lines) + "\n", states


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("displays_dir")
    ap.add_argument("rest", nargs="+", help="[scenario.json] out_dir")
    ap.add_argument("--scale", type=int, default=6)
    ap.add_argument("--tint", default="white", choices=sorted(oledlook.TINTS))
    ap.add_argument("--only", default=None, help="only states whose name contains this")
    ap.add_argument("--no-sheet", action="store_true")
    ap.add_argument("--no-glow", action="store_true")
    ap.add_argument("--title", default=None)
    ap.add_argument("--lang", default=None, help="language of the HUD's words (es, en, zh) for states without \"lang\"")
    a = ap.parse_args()
    if len(a.rest) == 1:
        scen, out = os.path.join(HERE, "scenarios", "standard.json"), a.rest[0]
    elif len(a.rest) == 2:
        scen, out = a.rest
    else:
        ap.error("expected [scenario.json] out_dir")
    scen = os.path.abspath(scen)
    out = os.path.abspath(out)

    r = subprocess.run([os.path.join(HERE, "build.sh"), a.displays_dir], stdout=subprocess.PIPE, text=True)
    if r.returncode != 0:
        sys.exit("build failed")
    binary = r.stdout.strip().splitlines()[-1]

    sc = load_scenario(scen)
    plan, states = make_plan(sc, os.path.dirname(scen), a.only, a.lang)
    raw = os.path.join(out, "raw")
    if os.path.isdir(raw):
        shutil.rmtree(raw)
    os.makedirs(raw, exist_ok=True)
    plan_path = os.path.join(raw, "plan.txt")
    with open(plan_path, "w") as f:
        f.write(plan)
    r = subprocess.run([binary, plan_path, raw])
    if r.returncode != 0:
        print("simulator reported failures (see above)", file=sys.stderr)

    index, state_info = [], []
    for st in states:
        fp = os.path.join(raw, st["name"] + ".frames")
        if not os.path.exists(fp):
            continue
        meta, frames = oledlook.parse_frames(fp)
        if not frames:
            continue
        hooks = meta.get("hooks", "").split()
        base = {"state": st["name"], "millis": st["millis"], "warnings": meta.get("warnings", ""),
                "hooks": hooks, "spec": st["spec"]}
        ai = int(meta.get("at_index", 0))   # frame visible at "millis" (> 0 when capture started earlier)

        def emit(fr, name, extra=None):
            kw = dict(contrast=fr["contrast"], on=fr["on"], tint=a.tint)
            oledlook.image_oled(fr["px"], scale=a.scale, glow=not a.no_glow, **kw).save(os.path.join(out, name + ".png"))
            oledlook.image_1x(fr["px"], **kw).save(os.path.join(out, name + "_1x.png"))
            e = dict(base, name=name, file=name + ".png", file_1x=name + "_1x.png", t_ms=fr["t"],
                     shown_ms=fr["shown"], contrast=fr["contrast"], on=fr["on"], invert=fr["invert"],
                     lit_pixels=int(fr["px"].sum()))
            if extra:
                e.update(extra)
            index.append(e)

        # the picture at "millis" may have been captured earlier (boop strips): report millis' snapshot time
        at_t = {"t_ms": float(meta["at_t"])} if "at_t" in meta else {}
        if st["phases"]:
            fa = frames[ai]
            fb = frames[ai + 1] if len(frames) > ai + 1 else frames[ai]
            static = len(frames) == ai + 1
            emit(fa, st["name"] + "_a", dict(at_t, phase="a", static=static))
            emit(fb, st["name"] + "_b", {"phase": "b", "static": static})
        else:
            emit(frames[ai], st["name"], at_t)
        if st["gif"] and len(frames) >= 1:
            imgs, durs = [], []
            for i, fr in enumerate(frames):
                imgs.append(oledlook.image_oled(fr["px"], scale=max(2, a.scale // 2), contrast=fr["contrast"],
                                                on=fr["on"], tint=a.tint, glow=not a.no_glow))
                nxt = frames[i + 1]["t"] if i + 1 < len(frames) else st["millis"] + st["record"]
                durs.append(max(20, int(round(nxt - fr["t"]))))
            imgs[0].save(os.path.join(out, st["name"] + ".gif"), save_all=True, append_images=imgs[1:],
                         duration=durs, loop=0)
        if meta.get("warnings"):
            print(f"[{st['name']}] controller warnings: {meta['warnings']}", file=sys.stderr)

        blog = boopstrip.parse_boop_log(os.path.join(raw, st["name"] + ".boop"))
        info = {"state": st["name"], "hooks": hooks, "render_ms": float(meta.get("render_ms", 0) or 0),
                "loops": int(meta.get("loops", 0) or 0)}
        if st["boop"] is not None or blog["events"]:
            b = boopstrip.summarize(blog, st["boop"], end_ms=st["millis"] + st["record"])
            info["boop"] = b
            if st["boop"] is not None and st["boop"]["strip"]:
                sp = os.path.join(out, st["name"] + "_strip.png")
                boopstrip.strip(frames, blog, st["boop"], sp, title=st["name"], tint=a.tint,
                                glow=not a.no_glow, cols=st["boop"]["strip_cols"])
                info["strip"] = os.path.basename(sp)
            seq = ", ".join(f"{e['name']}@{e['rel_ms']:+d}" for e in b["events"]) or "(no gesture events)"
            print(f"[{st['name']}] {seq} | OLED {b['window']['oled_frames']} frames in "
                  f"{b['window']['to_ms'] - b['window']['from_ms']} ms window "
                  f"({b['window']['oled_frames_per_s']}/s), max frame gap {b['window']['max_frame_gap_ms']} ms")
        state_info.append(info)

    with open(os.path.join(out, "index.json"), "w") as f:
        json.dump({"design": os.path.abspath(a.displays_dir), "scenario": scen, "tint": a.tint, "lang": a.lang,
                   "scale": a.scale, "images": index, "states": state_info}, f, indent=1)
    if not a.no_sheet:
        title = a.title or os.path.abspath(a.displays_dir)
        sheet.contact_sheet(out, os.path.join(out, "contact_sheet.png"), title=title, tint=a.tint)
    print(f"{len(index)} images -> {out}")


if __name__ == "__main__":
    main()
