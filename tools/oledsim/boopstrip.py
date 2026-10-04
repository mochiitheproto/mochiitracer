"""Boop gesture helpers for render.py: parse the simulator's raw/<state>.boop log, summarize it
(events, OLED transfers, frame gaps and the bus budget during the gesture) and draw the strip
(every distinct OLED frame around the gesture, labelled with time and the latest event)."""
import json
import os
import statistics

from PIL import Image, ImageDraw

import oledlook
from faces import FACES
PROGRESS = ("tap1", "hold")              # events that do not end a gesture
ACTIVE = ("TAP", "GAP", "HOLD")          # stages where a stalled frame cancels the gesture
AFTER_MS = 1500                          # the gesture "window" lasts until 1.5 s after FIRE/cancel
BUDGET_MIN_INTERVAL_MS = 100             # <= 10 OLED frames/s during a gesture
STALL_MS = 100                           # BoopGesture::STALL_MS
HERE = os.path.dirname(os.path.abspath(__file__))

BG = (18, 20, 24)
FG = (230, 232, 236)
DIM = (140, 146, 156)
GOOD = (120, 230, 140)
WARM = (255, 190, 90)
BAD = (255, 110, 110)


def parse_boop_log(path):
    d = {"loops": [], "events": [], "stages": [], "faces": [], "samples": [], "xfers": []}
    if not os.path.exists(path):
        return d
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p or p[0].startswith("#"):
                continue
            k = p[0]
            if k == "L":
                d["loops"].append(int(p[1]))
            elif k == "E":
                d["events"].append({"t_ms": int(p[1]), "code": int(p[2]), "name": p[3], "dur_ms": int(p[4])})
            elif k == "S":
                d["stages"].append((int(p[1]), p[2]))
            elif k == "F":
                d["faces"].append((int(p[1]), int(p[2])))
            elif k == "B":
                d["samples"].append((int(p[1]), int(p[2])))
            elif k == "X":
                d["xfers"].append({"t_ms": float(p[1]), "bus_ms": float(p[2]), "data_bytes": int(p[3]),
                                   "cmd_bytes": int(p[4]), "bursts": int(p[5]), "delay_ms": float(p[6])})
    return d


def stage_at(stages, t):
    s = "WAIT"
    for ts, name in stages:
        if ts > t:
            break
        s = name
    return s


def expected(preset):
    if not preset:
        return None
    try:
        with open(os.path.join(HERE, "boop_presets.json")) as f:
            return json.load(f)["presets"][preset].get("expect")
    except (OSError, KeyError, ValueError):
        return None


def summarize(blog, bspec, end_ms):
    ev = blog["events"]
    if bspec is not None:
        start = bspec["boop_at"]
        last_edge = bspec["edges"][-1][0] if bspec["edges"] else start
    else:
        start = ev[0]["t_ms"] if ev else 0
        last_edge = start
    term = [e for e in ev if e["name"] not in PROGRESS]
    ws = start
    we = min(int(end_ms), (term[-1]["t_ms"] if term else last_edge) + AFTER_MS)

    loops = blog["loops"]
    gaps, gaps_active = [], []
    for i in range(1, len(loops)):
        if ws <= loops[i] <= we:
            g = loops[i] - loops[i - 1]
            gaps.append(g)
            if stage_at(blog["stages"], loops[i - 1]) in ACTIVE:
                gaps_active.append(g)
    xs = blog["xfers"]
    data = [x for x in xs if x["data_bytes"] > 0]
    data_in = [x for x in data if ws <= x["t_ms"] <= we]
    intervals = [round(b["t_ms"] - a["t_ms"], 3) for a, b in zip(data, data[1:]) if ws <= b["t_ms"] <= we]
    loop_with_data = []   # duration of the loop that carried each OLED frame in the window
    for x in data_in:
        for i in range(1, len(loops)):
            if loops[i - 1] <= x["t_ms"] < loops[i]:
                loop_with_data.append(loops[i] - loops[i - 1])
                break
    win_s = max(1e-9, (we - ws) / 1000.0)
    w = {
        "from_ms": ws, "to_ms": we,
        "loops": len(gaps),
        "typical_frame_ms": float(statistics.median(gaps)) if gaps else None,
        "max_frame_gap_ms": max(gaps) if gaps else None,
        "max_frame_gap_active_ms": max(gaps_active) if gaps_active else None,
        "max_frame_ms_with_oled": max(loop_with_data) if loop_with_data else None,
        "oled_frames": len(data_in),
        "oled_frames_per_s": round(len(data_in) / win_s, 2),
        "min_oled_interval_ms": min(intervals) if intervals else None,
        "oled_cmd_only": len([x for x in xs if x["data_bytes"] == 0 and x["cmd_bytes"] > 0 and ws <= x["t_ms"] <= we]),
        "loops_with_2plus_displays": len([x for x in xs if x["bursts"] > 1 and ws <= x["t_ms"] <= we]),
        "hud_delay_ms_max": max([x["delay_ms"] for x in xs if ws <= x["t_ms"] <= we] or [0.0]),
    }
    problems = []
    if w["min_oled_interval_ms"] is not None and w["min_oled_interval_ms"] < BUDGET_MIN_INTERVAL_MS:
        problems.append(f"OLED frames {w['min_oled_interval_ms']} ms apart (< {BUDGET_MIN_INTERVAL_MS})")
    if w["loops_with_2plus_displays"]:
        problems.append(f"{w['loops_with_2plus_displays']} loop(s) with 2+ display() bursts")
    if w["hud_delay_ms_max"] > 0:
        problems.append(f"HUD blocked/delayed {w['hud_delay_ms_max']} ms in one loop")
    if w["max_frame_gap_active_ms"] is not None and w["max_frame_gap_active_ms"] > STALL_MS:
        problems.append(f"frame gap {w['max_frame_gap_active_ms']} ms mid-gesture (> STALL {STALL_MS})")
    w["budget_ok"] = not problems
    w["budget_problems"] = problems

    names = [e["name"] for e in ev]
    exp = expected(bspec["preset"]) if bspec else None
    if bspec is not None and not bspec.get("sensor_on", True):
        exp = []          # BoopSensor off in the menu: the head resets the gesture every frame
    out = {
        "preset": bspec["preset"] if bspec else None,
        "boop_at": start,
        "edges": bspec["edges"] if bspec else [],
        "events": [dict(e, rel_ms=e["t_ms"] - start) for e in ev],
        "face_changes": [{"t_ms": t, "rel_ms": t - start, "face": FACES[f] if f < len(FACES) else f}
                         for t, f in blog["faces"]],
        "stages": [{"t_ms": t, "rel_ms": t - start, "stage": s} for t, s in blog["stages"]],
        "sensor_samples": [{"t_ms": t, "booped": v} for t, v in blog["samples"]],
        "window": w,
        "oled_transfers": xs,
    }
    if exp is not None:
        out["expected_events"] = exp
        out["events_as_expected"] = names == exp
    return out


def _event_color(name):
    if name == "FIRE":
        return GOOD
    if name in PROGRESS:
        return WARM
    return BAD


def strip(frames, blog, bspec, path, title="", tint="white", glow=True, cols=4, scale=6):
    """Every distinct frame visible in [strip_from, strip_to], 6x OLED look, labelled with the
    time relative to the boop start and the latest gesture event at that moment."""
    ws, we, start = bspec["strip_from"], bspec["strip_to"], bspec["boop_at"]
    sel = []
    for i, fr in enumerate(frames):
        tv = fr["tx"]
        if i == 0 and fr["t"] <= ws + 40:   # first snapshot after the capture start = the picture at ws
            tv = min(tv, ws)
        if tv <= ws:
            sel = [(fr, ws)]          # the picture on the panel when the strip starts
        elif tv <= we:
            sel.append((fr, tv))
    if not sel:
        return None
    ev = blog["events"]
    tiles = [oledlook.image_oled(fr["px"], scale=scale, contrast=fr["contrast"], on=fr["on"], tint=tint,
                                 glow=glow, margin=scale * 2) for fr, _ in sel]
    tw, th = tiles[0].size
    pad, lab, head = 16, 104, 120
    cols = max(1, min(cols, len(tiles)))
    rows = (len(tiles) + cols - 1) // cols
    W = cols * (tw + pad) + pad
    H = head + rows * (th + lab + pad) + pad
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.text((pad, 12), title, fill=FG, font=oledlook.font(30, True))
    pat = bspec["preset"] or "edges"
    edges = "  ".join(f"{'on' if v else 'off'}@{t - start:+d}" for t, v in bspec["edges"])
    d.text((pad, 52), f"boop {pat} at {start} ms:  {edges}", fill=DIM, font=oledlook.font(18))
    seq = "   ".join(f"{e['name']} {e['t_ms'] - start:+d}" for e in ev if ws - 1000 <= e["t_ms"] <= we) or "no gesture events"
    d.text((pad, 80), "events: " + seq, fill=WARM, font=oledlook.font(18, True))
    for i, ((fr, tv), t) in enumerate(zip(sel, tiles)):
        r, c = divmod(i, cols)
        x = pad + c * (tw + pad)
        y = head + r * (th + lab + pad)
        img.paste(t, (x, y))
        last = None
        for e in ev:
            if e["t_ms"] <= tv:
                last = e
        rel = int(round(tv - start))
        d.text((x + 4, y + th + 6), f"{rel:+d} ms", fill=FG, font=oledlook.font(28, True))
        if last is not None:
            d.text((x + 170, y + th + 8), f"{last['name']}  ({last['t_ms'] - start:+d})", fill=_event_color(last["name"]),
                   font=oledlook.font(24, True))
        else:
            d.text((x + 170, y + th + 8), "-", fill=DIM, font=oledlook.font(24, True))
        st = stage_at(blog["stages"], tv)
        hold = ""
        if st == "HOLD":
            hs = [ts for ts, s in blog["stages"] if s == "HOLD" and ts <= tv]
            if hs:
                hold = f" {int(tv - hs[-1])} ms"
        d.text((x + 4, y + th + 46), f"stage {st}{hold}   contrast 0x{fr['contrast']:02X}{'' if fr['on'] else '   OFF'}",
               fill=DIM, font=oledlook.font(20))
    img.save(path)
    return path
