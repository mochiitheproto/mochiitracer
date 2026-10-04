#!/usr/bin/env python3
"""protorender — previews de caras del protogen con el motor REAL de ProtoTracer en host.

Subcomandos:
  render      receta(s) o cara nativa → .txt (formato captura) + .png LED [espejo|normal]
  transition  receta A → receta B, N frames (EasyEase real, 125 fps) → tira PNG (+ GIF)
  validate    reproduce las caras nativas y compara contra cap/ref (IoU con shift ±3)
  verts       posiciones de los 54 vértices (modelo y pixel) de una receta
  export      imprime el C++ de un morph nuevo listo pa pegar en un header de mesh

Ver README.md.
"""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "bin", "hostrender")
NUKUDE_H = os.path.join(os.environ.get("PROTOTRACER_LIB") or os.path.join(HERE, "..", "..", "lib", "ProtoTracer"),
                        "Assets", "Models", "FBX", "NukudeFlat.h")  # el mismo lib que compila build.sh
REF = os.path.join(os.path.dirname(HERE), "cap", "ref")
W, H = 64, 32
FPS = 125.0            # frameLimiter TimeStep(120) → intervalo uint16(8.33)=8 ms → 125 fps

MORPHS = ["Frown", "Doubt", "Surprised", "Sadness", "Anger", "vrc_v_sil", "vrc_v_th", "vrc_v_nn",
          "vrc_v_ss", "vrc_v_rr", "vrc_v_dd", "vrc_v_kk", "vrc_v_ff", "vrc_v_pp", "vrc_v_ch",
          "vrc_v_ou", "vrc_v_oh", "vrc_v_ih", "vrc_v_ee", "vrc_v_aa", "LookDown", "LookUp", "Blink",
          "BiggerNose", "MoveEye", "HideBlush"]

# Caras nativas = lo que hacen sus métodos en ProtogenHUB75Project.h (AddParameterFrame/AddMaterialFrame).
NATIVE = {
    "DEFAULT":   {},
    "ANGRY":     {"weights": {"Anger": 1.0}, "material": ["CRED", 0.8]},
    "DOUBT":     {"weights": {"Doubt": 1.0}},
    "FROWN":     {"weights": {"Frown": 1.0}},
    "LOOKUP":    {"weights": {"LookUp": 1.0}},
    "SAD":       {"weights": {"Sadness": 1.0, "Frown": 1.0}, "material": ["CBLUE", 0.8]},
    "LOOKDOWN":  {"weights": {"LookDown": 1.0}},
    "SURPRISED": {"weights": {"Surprised": 1.0, "HideBlush": 0.0}, "material": ["CRAINBOW", 0.8]},
    # TachaFace(): AddParameterFrame(NukudeFace::vrc_v_aa, 0.7) NO hace nada en el firmware
    # (vrc_v_aa está ligado como viseme AH = dict 105, no 19). Se deja igual pa reproducirlo.
    "TACHA":     {"weights": {"vrc_v_aa": 0.7, "Surprised": 0.4, "HideBlush": 0.0}, "material": ["CRAINBOW", 0.8]},
}
CAPTURES = {"DEFAULT": "00", "ANGRY": "01", "DOUBT": "02", "FROWN": "03", "LOOKUP": "04", "SAD": "05", "TACHA": "10"}

DEFAULT_SETTINGS = {"facesize": 9, "color": 2, "hue": [5, 2], "wiggle": "off", "blink": "off",
                    "mic_ss": None, "time_ms": 20000}


# ----------------------------------------------------------------------------- recetas
def _load(x, base_dir):
    if isinstance(x, str):
        p = x if os.path.isabs(x) else os.path.join(base_dir, x)
        if not os.path.exists(p):
            p = os.path.join(HERE, x)
        with open(p) as f:
            return json.load(f), os.path.dirname(os.path.abspath(p))
    return x, base_dir


def load_recipe(spec):
    """spec: ruta a .json, nombre de cara nativa (DEFAULT, TACHA...), o dict."""
    if isinstance(spec, dict):
        r, d = dict(spec), HERE
    elif spec.upper() in NATIVE and not os.path.exists(spec):
        r, d = dict(NATIVE[spec.upper()]), HERE
        r.setdefault("name", spec.upper())
    else:
        r, d = _load(spec, os.getcwd())
        r = dict(r)
    if "base" in r:                      # hereda de una cara nativa u otra receta
        b = load_recipe(r["base"])
        merged = dict(b)
        merged["weights"] = {**b.get("weights", {}), **r.get("weights", {})}
        merged["visemes"] = {**b.get("visemes", {}), **r.get("visemes", {})}
        for k, v in r.items():
            if k not in ("weights", "visemes", "base"):
                merged[k] = v
        r = merged
    r["_dir"] = d
    r.setdefault("name", os.path.splitext(os.path.basename(spec))[0] if isinstance(spec, str) else "receta")
    return r


def _materials(r):
    m = r.get("material")
    if m is None:
        return []
    if isinstance(m, str):
        return [(m, 0.8)]                         # AddMaterialFrame(Color) default opacity 0.8
    if isinstance(m, list) and m and isinstance(m[0], str):
        return [(m[0], float(m[1]) if len(m) > 1 else 0.8)]
    return [(x[0], float(x[1]) if len(x) > 1 else 0.8) if isinstance(x, list) else (x, 0.8) for x in m]


def morph_defs(r):
    """Morphs nuevos de la receta, ya en deltas de modelo. Los que traen
    'pixel_positions' se resuelven corriendo el motor (ver solve_pixels)."""
    if "_morphs" not in r:
        r["_morphs"] = [resolve_morph(_load(m, r["_dir"])[0]) for m in r.get("morphs", [])]
        r["_objects"] = [resolve_object(_load(o, r["_dir"])[0]) for o in r.get("objects", [])]
        if any(m["pixel_positions"] or m["pin"] for m in r["_morphs"]):
            solve_pixels(r)
    return r["_morphs"]


def base_vertices():
    """Lee basisVertices de NukudeFlat.h (los 54 vértices de modelo)."""
    import re
    p = NUKUDE_H
    s = open(p).read()
    blk = s[s.index("basisVertices[54]"):]
    blk = blk[:blk.index("};")]
    return [tuple(float(v) for v in t) for t in re.findall(r"Vector3D\(([-\d.]+)f,([-\d.]+)f,([-\d.]+)f\)", blk)]


def resolve_morph(md):
    """Morph nuevo. Formas de dar el desplazamiento (se pueden combinar):
      "indices"+"deltas" (listas paralelas) o "deltas": {idx: [dx,dy,dz]}   → delta en modelo
      "positions": {idx: [x,y(,z)]}                                         → posición absoluta en modelo
      "pixel_positions": {idx: [col,renglón]}                               → posición en el panel normal
    (col 0..63 izq→der, renglón 0..31 arriba→abajo, centros de LED en enteros)."""
    name = md["name"]
    idx, vec = [], []
    if "indices" in md and "deltas" in md:
        idx = [int(i) for i in md["indices"]]
        vec = [list(map(float, d)) + [0.0] * (3 - len(d)) for d in md["deltas"]]
    elif isinstance(md.get("deltas"), dict):
        for k, d in md["deltas"].items():
            idx.append(int(k)); vec.append(list(map(float, d)) + [0.0] * (3 - len(d)))
    bv = base_vertices()
    for k, p in md.get("positions", {}).items():
        i = int(k)
        p = list(map(float, p))
        tgt = [p[0], p[1], p[2] if len(p) > 2 else bv[i][2]]
        d = [tgt[j] - bv[i][j] for j in range(3)]
        if i in idx:
            vec[idx.index(i)] = d
        else:
            idx.append(i); vec.append(d)
    pix = {int(k): list(map(float, v)) for k, v in md.get("pixel_positions", {}).items()}
    for i in pix:
        if i not in idx:
            idx.append(i); vec.append([0.0, 0.0, 0.0])
    pin = md.get("pin", [])
    if pin == "all":     # todo lo que no es chapita (41-52, colapsadas por HideBlush) ni ya tiene destino
        pin = [i for i in list(range(0, 41)) + [53] if i not in pix]
    pin = [int(i) for i in pin if int(i) not in pix]
    for i in pin:
        if i not in idx:
            idx.append(i); vec.append([0.0, 0.0, 0.0])
    return {"name": name, "indices": idx, "deltas": vec, "pixel_positions": pix, "pin": pin,
            "frames": int(md.get("frames", 15)), "interp": md.get("interp", "Overshoot")}


def resolve_object(od):
    """Objeto extra (geometría nueva, p.ej. una X, gota, corazón). "space":
      "pixel"  (default) vértices [col, renglón(, z)] del panel normal → coords de cámara
               X=col*3, Y=(31-renglón)*3; z default 0 (delante de la cara, que vive en z≈200-300).
               Sigue el wiggle de la cara. NO afecta la alineación de la cara.
      "camera" coords de cámara crudas (X 0..189, Y 0..93, z). Wiggle sólo si "wiggle": true.
      "model"  coords de NukudeFlat; se alinea JUNTO con la cara (AlignObjectsFace) → OJO: mueve
               el plano PCA/bbox y puede inclinar o re-escalar toda la cara."""
    o = dict(od)
    sp = od.get("space", "pixel")
    if sp == "pixel":
        o["vertices"] = [[float(v[0]) * 3.0, (31.0 - float(v[1])) * 3.0, float(v[2]) if len(v) > 2 else 0.0]
                         for v in od["vertices"]]
        o["space"] = "camera_wiggle" if od.get("wiggle", True) else "camera"
    elif sp == "camera":
        o["vertices"] = [list(map(float, v)) + [0.0] * (3 - len(v)) for v in od["vertices"]]
        o["space"] = "camera_wiggle" if od.get("wiggle", False) else "camera"
    else:
        o["vertices"] = [list(map(float, v)) + [0.0] * (3 - len(v)) for v in od["vertices"]]
        o["space"] = "model"
    return o


def object_defs(r):
    morph_defs(r)
    return r["_objects"]


def _verts_run(r, settings, frames):
    L = header_lines(settings, [r]) + target_lines(r, all_object_names([r])) + [f"run {frames} -1", "verts"]
    _, raw = run_script(L)
    return parse_verts(raw)


def parse_verts(raw):
    import numpy as np
    out, cur, name = {}, None, None
    for ln in raw.splitlines():
        if ln.startswith("VERTS "):
            name, cur = ln.split()[1], []
        elif ln == "END" and cur is not None:
            out[name] = np.array(cur)
            cur = None
        elif cur is not None:
            p = ln.split()
            m = [float(v) for v in p[6].split("=")[1].split(",")]
            cur.append([float(p[1]), float(p[2]), float(p[3]),
                        float(p[4].split("=")[1]), float(p[5].split("=")[1])] + m)
    return out   # columnas: X Y Z (cámara), col, renglón, mx, my, mz (modelo con morphs)


def fit_affine(V):
    """modelo(mx,my,mz) → (col, renglón): afín exacto (la cámara es ortográfica)."""
    import numpy as np
    M = np.c_[V[:, 5:8], np.ones(len(V))]
    C, *_ = np.linalg.lstsq(M, V[:, 3:5], rcond=None)
    return C[:3].T, C[3]          # A (2x3), b (2)


def plane_dir(M):
    """Réplica de ObjectAlign::GetPlaneOrientation (sin el offset de -7.5°): normal del plano
    que el firmware usa pa 'aplanar' la malla antes de alinearla."""
    import numpy as np
    o = M - M.mean(0)
    xx, xy, xz = (o[:, 0] * o[:, 0]).sum(), (o[:, 0] * o[:, 1]).sum(), (o[:, 0] * o[:, 2]).sum()
    yy, yz, zz = (o[:, 1] * o[:, 1]).sum(), (o[:, 1] * o[:, 2]).sum(), (o[:, 2] * o[:, 2]).sum()
    xD, yD, zD = yy * zz - yz * yz, xx * zz - xz * xz, xx * yy - xy * xy
    m = max(xD, yD, zD)
    if abs(m - xD) < 1e-3:
        d = np.array([xD, xz * yz - xy * zz, xy * yz - xz * yy])
    elif abs(m - yD) < 1e-3:
        d = np.array([xz * yz - xy * zz, yD, xy * xz - yz * xx])
    else:
        d = np.array([xy * yz - xz * yy, xy * xz - yz * xx, zD])
    return d / np.linalg.norm(d)


def keep_plane(M, free, n_view, ref_dir, steps=6, lam=1e-7, max_step=40.0):
    """Offsets t (a lo largo de la dirección de vista, que no mueve pixeles) para los vértices
    `free` tal que la normal del plano vuelva a ref_dir. Gauss-Newton amortiguado. {idx: t}."""
    import numpy as np
    t = np.zeros(len(free))

    def f(tv):
        Q = M.copy()
        Q[free] += np.outer(tv, n_view)
        d = plane_dir(Q)
        d = d if d @ ref_dir >= 0 else -d
        return d - ref_dir
    r0 = f(t)
    for _ in range(steps):
        if not np.isfinite(r0).all() or np.abs(r0).max() < 1e-6:
            break
        J = np.zeros((3, len(free)))
        for j in range(len(free)):
            dt = np.zeros(len(free)); dt[j] = 1e-2
            J[:, j] = (f(t + dt) - r0) / 1e-2
        step = -J.T @ np.linalg.solve(J @ J.T + lam * np.eye(3), r0)
        if np.abs(step).max() > max_step:
            step *= max_step / np.abs(step).max()
        t_new = t + step
        r_new = f(t_new)
        if np.isfinite(r_new).all() and np.abs(r_new).max() < np.abs(r0).max():
            t, r0 = t_new, r_new
        else:
            lam *= 10
    return dict(zip(free, t))


def solve_pixels(r, iters=12, tol=0.03, gain=0.9, keep_plane_on=True):
    """Resuelve deltas (morph pixel_positions / pin) y vértices (objetos space=pixel) iterando
    contra el motor real: incluye la re-alineación (bbox + plano PCA + stretch) que provoca
    mover vértices. 'pin' fija otros vértices en donde estaban sin el morph (compensación)."""
    import numpy as np
    s = settings_of(r)
    frames = int(1.5 * FPS)
    pend = [m for m in r["_morphs"] if m["pixel_positions"] or m["pin"]]
    w = dict(r.get("weights", {}))
    for m in pend:
        if float(w.get(m["name"], 0.0)) <= 0.0:
            w[m["name"]] = 1.0
    on = dict(r.get("objects_on", {}))

    def ctx(weights, objects_on):
        rr = dict(r)
        rr["weights"], rr["objects_on"] = weights, objects_on
        rr["_morphs"], rr["_objects"] = r["_morphs"], r["_objects"]
        return rr

    # destinos de los vértices "pin": donde quedan SIN estos morphs ni objetos nuevos
    w0 = {k: (0.0 if any(m["name"] == k for m in pend) else v) for k, v in w.items()}
    V0 = _verts_run(ctx(w0, on), s, frames)
    targets = []          # (morph, idx, target px)
    for m in pend:
        for i, tgt in m["pixel_positions"].items():
            targets.append((m, i, np.array(tgt)))
        for i in m["pin"]:
            targets.append((m, i, V0["pM"][i, 3:5].copy()))
    rr = ctx(w, on)
    ref_dir = plane_dir(V0["pM"][:, 5:8])
    err = 0.0
    for it in range(iters):
        V = _verts_run(rr, s, frames)
        A, b = fit_affine(V["pM"])
        Ap = np.linalg.pinv(A)
        n_view = np.linalg.svd(A)[2][-1]          # núcleo de A = dirección de vista en modelo
        M = V["pM"][:, 5:8].copy()
        err = 0.0
        for m, i, tgt in targets:
            k = m["indices"].index(i)
            e = tgt - V["pM"][i, 3:5]
            err = max(err, float(np.abs(e).max()))
            wgt = max(1e-3, float(w.get(m["name"], 1.0)) - 0.007)   # el spring asienta ~0.007 abajo
            step = gain * (Ap @ e)
            m["deltas"][k] = list(np.array(m["deltas"][k]) + step / wgt)
            M[i] += step
        # mover vértices en el plano de pantalla cambia la normal del plano (PCA) con la que
        # el firmware aplana la malla → se compensa con profundidad (no cambia pixeles)
        if pend and keep_plane_on:
            free = sorted({i for m in pend for i in m["indices"]})
            tt = keep_plane(M, free, n_view, ref_dir)
            for m in pend:
                wgt = max(1e-3, float(w.get(m["name"], 1.0)) - 0.007)
                for i, tv in tt.items():
                    if i in m["indices"]:
                        k = m["indices"].index(i)
                        m["deltas"][k] = list(np.array(m["deltas"][k]) + tv * n_view / wgt)
                break   # la compensación se aplica a un solo morph
        if err < tol:
            break
    r["_solve_err_px"] = err
    r["_solve_iters"] = it + 1
    if err > 0.25:
        print(f"  aviso: pixel_positions no convergió (error {err:.2f} px tras {it + 1} iteraciones); "
              f"prueba \"pin\": \"all\" o deja algún vértice en el borde del bbox", file=sys.stderr)


def settings_of(r, override=None):
    s = dict(DEFAULT_SETTINGS)
    s.update(r.get("settings", {}))
    if override:
        s.update({k: v for k, v in override.items() if v is not None})
    return s


# ----------------------------------------------------------------------------- script
def header_lines(settings, recipes):
    """set + link + morph/object defs (unión de todo lo que usen las recetas)."""
    L = []
    s = settings
    L.append(f"set facesize {int(s['facesize'])}")
    L.append(f"set color {int(s['color'])}")
    L.append(f"set hue {int(s['hue'][0])} {int(s['hue'][1])}")
    L.append(f"set time_ms {int(s['time_ms'])}")
    w = s["wiggle"]
    L.append("set wiggle " + (f"{w[0]} {w[1]}" if isinstance(w, (list, tuple)) else str(w)))
    L.append(f"set blink {s['blink']}")
    if s.get("mic_ss") is not None:
        L.append(f"set mic_ss {float(s['mic_ss'])}")
    seen_link, seen_m, seen_o = set(), set(), set()
    for r in recipes:
        for lk in r.get("link", []):
            lk = lk if isinstance(lk, list) else [lk]
            if lk[0] in seen_link:
                continue
            seen_link.add(lk[0])
            L.append("link " + " ".join(str(x) for x in lk))
        for md in morph_defs(r):
            if md["name"] in seen_m:
                continue
            seen_m.add(md["name"])
            L.append(f"morph {md['name']} {len(md['indices'])} {md['frames']} {md['interp']}")
            for i, d in zip(md["indices"], md["deltas"]):
                L.append(f"{i} {d[0]:.4f} {d[1]:.4f} {d[2]:.4f}")
        for od in object_defs(r):
            if od["name"] in seen_o:
                continue
            seen_o.add(od["name"])
            mat = od.get("material", "face")
            mats = "face" if mat == "face" else "rgb " + " ".join(str(int(c)) for c in mat)
            L.append(f"object {od['name']} {od['space']} {len(od['vertices'])} {len(od['triangles'])} {mats}")
            for v in od["vertices"]:
                v = list(v) + [0.0] * (3 - len(v))
                L.append(f"{v[0]} {v[1]} {v[2]}")
            for t in od["triangles"]:
                L.append(f"{t[0]} {t[1]} {t[2]}")
    return L


def target_lines(r, all_objects):
    L = ["target clear"]
    for k, v in r.get("weights", {}).items():
        L.append(f"target param {k} {float(v)}")
    for k, v in r.get("visemes", {}).items():
        L.append(f"target viseme {k} {float(v)}")
    for c, o in _materials(r):
        L.append(f"target material {c} {o}")
    on = {od["name"]: od.get("enabled", True) for od in object_defs(r)}
    on.update(r.get("objects_on", {}))
    for name in all_objects:
        L.append(f"target object {name} {'on' if on.get(name, False) else 'off'}")
    return L


def run_script(lines):
    if not os.path.exists(BIN):
        sys.exit(f"falta {BIN}: corre ./build.sh")
    p = subprocess.run([BIN], input="\n".join(lines) + "\n", capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(p.stderr)
    for ln in p.stderr.splitlines():
        print("  hostrender:", ln, file=sys.stderr)
    return parse_frames(p.stdout), p.stdout


def parse_frames(text):
    frames, cur, meta, weights = [], None, None, None
    for ln in text.splitlines():
        if ln.startswith("FRAME"):
            cur, meta, weights = [], ln, {}
        elif ln.startswith("W ") and cur is not None:
            for kv in ln[2:].split():
                k, v = kv.split("=")
                weights[k] = float(v)
        elif ln == "END" and cur is not None:
            frames.append({"meta": meta, "weights": weights, "pix": cur})
            cur = None
        elif cur is not None and len(ln) == W * 6:
            cur.append([tuple(int(ln[x * 6 + k * 2: x * 6 + k * 2 + 2], 16) for k in range(3)) for x in range(W)])
    return frames


def all_object_names(recipes):
    names = []
    for r in recipes:
        for od in object_defs(r):
            if od["name"] not in names:
                names.append(od["name"])
    return names


def simulate(recipes_frames, settings, every=0):
    """recipes_frames: [(receta, n_frames), ...] en orden. Emite el último frame de cada
    fase (every=0) o cada `every` frames. Devuelve lista de (fase, frame)."""
    recs = [r for r, _ in recipes_frames]
    objs = all_object_names(recs)
    L = header_lines(settings, recs)
    tags = []
    for i, (r, n) in enumerate(recipes_frames):
        L += target_lines(r, objs)
        e = every if (every and i == len(recipes_frames) - 1) else 0
        L.append(f"run {n} {e}")
        tags += [i] * (n // e if e else 1)
    frames, raw = run_script(L)
    return list(zip(tags, frames)), raw


# ----------------------------------------------------------------------------- imágenes
def led_render(img, s=8, gap=2, sep=24):
    """Igual que captura.py: [espejo | normal], LED apagado = gris (28,28,28)."""
    mir = [row[::-1] for row in img]
    out_w = 2 * W * s + sep
    out = [[(12, 12, 12)] * out_w for _ in range(H * s)]
    for panel, ox in ((mir, 0), (img, W * s + sep)):
        for y in range(H):
            for x in range(W):
                c = panel[y][x]
                c = c if any(c) else (28, 28, 28)
                for dy in range(s - gap):
                    row = out[y * s + dy]
                    for dx in range(s - gap):
                        row[ox + x * s + dx] = c
    return out


def to_pil(rows):
    from PIL import Image
    h, w = len(rows), len(rows[0])
    im = Image.new("RGB", (w, h))
    im.putdata([p for r in rows for p in r])
    return im


def save_txt(path, pix):
    with open(path, "w") as f:
        for r in pix:
            f.write("".join("%02X%02X%02X" % c for c in r) + "\n")


def load_txt(path):
    rows = [l.strip() for l in open(path) if l.strip()]
    return [[tuple(int(r[x * 6 + k * 2: x * 6 + k * 2 + 2], 16) for k in range(3)) for x in range(W)] for r in rows]


def label_strip(images, labels, pad=6, bg=(12, 12, 12)):
    from PIL import Image, ImageDraw
    th = 14
    w = max(i.width for i in images)
    h = sum(i.height + th + pad for i in images)
    out = Image.new("RGB", (w, h), bg)
    d = ImageDraw.Draw(out)
    y = 0
    for im, lb in zip(images, labels):
        d.text((4, y + 1), lb, fill=(200, 200, 200))
        out.paste(im, (0, y + th))
        y += im.height + th + pad
    return out


# ----------------------------------------------------------------------------- métricas
def lit(img):
    return [[1 if any(c) else 0 for c in r] for r in img]


def _np_lit(img):
    import numpy as np
    return np.array([[1 if any(c) else 0 for c in r] for r in img], dtype=bool)


def iou_shift(a, b, maxs=3):
    """IoU de pixeles prendidos de a (render) contra b (captura), mejor shift entero |d|<=maxs.
    Devuelve (iou, dx, dy): a desplazado dx columnas a la derecha y dy renglones hacia abajo."""
    import numpy as np
    A = a if isinstance(a, np.ndarray) else _np_lit(a)
    B = b if isinstance(b, np.ndarray) else _np_lit(b)
    best = (-1.0, 0, 0)
    for dy in range(-maxs, maxs + 1):
        for dx in range(-maxs, maxs + 1):
            S = np.zeros_like(A)
            ys0, ys1 = max(0, dy), min(H, H + dy)
            xs0, xs1 = max(0, dx), min(W, W + dx)
            S[ys0:ys1, xs0:xs1] = A[ys0 - dy:ys1 - dy, xs0 - dx:xs1 - dx]
            uni = np.logical_or(S, B).sum()
            v = np.logical_and(S, B).sum() / uni if uni else 1.0
            # desempate: preferir el shift más chico
            if v > best[0] + 1e-12 or (abs(v - best[0]) <= 1e-12 and abs(dx) + abs(dy) < abs(best[1]) + abs(best[2])):
                best = (float(v), dx, dy)
    return best


def mean_color(img):
    px = [c for r in img for c in r if any(c)]
    if not px:
        return (0, 0, 0)
    return tuple(round(sum(p[k] for p in px) / len(px)) for k in range(3))


# ----------------------------------------------------------------------------- comandos
def cmd_render(a):
    os.makedirs(a.out, exist_ok=True)
    for spec in a.recipes:
        r = load_recipe(spec)
        s = settings_of(r, {"facesize": a.facesize, "wiggle": a.wiggle})
        warm = load_recipe(a.warmup) if a.warmup else load_recipe("DEFAULT")
        res, _ = simulate([(warm, a.warm_frames), (r, a.frames)], s)
        f = res[-1][1]
        base = os.path.join(a.out, r["name"])
        save_txt(base + ".txt", f["pix"])
        to_pil(led_render(f["pix"])).save(base + ".png")
        nz = {k: round(v, 3) for k, v in f["weights"].items() if abs(v) > 1e-3}
        print(f"{r['name']}: {base}.png  ({sum(map(sum, lit(f['pix'])))} LEDs, pesos {nz})")


def cmd_transition(a):
    os.makedirs(a.out, exist_ok=True)
    ra, rb = load_recipe(a.a), load_recipe(a.b)
    s = settings_of(rb, {"facesize": a.facesize, "wiggle": a.wiggle})
    res, _ = simulate([(ra, a.warm_frames), (rb, a.frames)], s, every=a.every)
    frames = [f for tag, f in res if tag == 1]
    # frame 0 = A asentada
    a_res, _ = simulate([(ra, a.warm_frames)], s)
    seq = [a_res[-1][1]] + frames
    imgs = [to_pil(led_render(f["pix"], s=4, gap=1, sep=12)) for f in seq]
    labels = ["t=0 ms  " + ra["name"]] + [f"t={int(round((i + 1) * a.every * 1000 / FPS))} ms" for i in range(len(frames))]
    name = a.name or f"{ra['name']}_a_{rb['name']}"
    strip = label_strip(imgs, labels)
    strip.save(os.path.join(a.out, name + "_tira.png"))
    big = [to_pil(led_render(f["pix"])) for f in seq]
    big[0].save(os.path.join(a.out, name + ".gif"), save_all=True, append_images=big[1:] + [big[-1]] * 6,
                duration=int(a.every * 1000 / FPS), loop=0)
    print(f"{len(seq)} frames → {os.path.join(a.out, name + '_tira.png')} y .gif")


def sweep_frames(r, settings, rng=2.0, step=0.25, warm="DEFAULT", warm_frames=250, frames=None):
    """Simula warm→r y re-renderiza el último estado con wiggle (wx,wy) en [-rng,rng]²."""
    frames = frames or int(1.5 * FPS)
    w = load_recipe(warm)
    objs = all_object_names([w, r])
    L = header_lines(settings, [w, r])
    L += target_lines(w, objs) + [f"run {warm_frames} -1"]
    L += target_lines(r, objs) + [f"run {frames} -1"]
    L.append(f"sweep {-rng} {rng} {-rng} {rng} {step}")
    fr, _ = run_script(L)
    out = []
    for f in fr:
        wx, wy = (float(v) for v in f["meta"].split("wig=")[1].split(","))
        out.append(((round(wx, 3), round(wy, 3)), f))
    return out


def cmd_validate(a):
    import numpy as np
    faces = a.faces or list(CAPTURES)
    faltan = [f"cara{CAPTURES[n]}_0.txt" for n in faces
              if n in CAPTURES and not os.path.exists(os.path.join(REF, f"cara{CAPTURES[n]}_0.txt"))]
    if faltan:
        # las capturas son de una cabeza en particular: no vienen en el repo
        sys.exit(f"validate compara contra capturas reales de TU cabeza y faltan en {REF}: "
                 f"{', '.join(faltan)}. Sácalas con tools/captura.py "
                 f"{' '.join(str(int(CAPTURES[n])) for n in faces if n in CAPTURES)} "
                 "(deja cada cara su --asentar) y copia sus cara<N>_0.txt ahí.")
    os.makedirs(a.out, exist_ok=True)
    rows = []
    panels, labels = [], []
    for name in faces:
        nn = CAPTURES[name]
        ref = load_txt(os.path.join(REF, f"cara{nn}_0.txt"))
        B = _np_lit(ref)
        r = load_recipe(name)
        s = settings_of(r, {"facesize": a.facesize})
        sw = sweep_frames(r, s, rng=2.0, step=a.step)
        # (1) "int": wiggle=0 y mejor shift entero ±3 px (métrica pedida originalmente)
        # (2) "sub0": wiggle subpixel dentro del rango real del firmware (±2 u = ±0.67 px), SIN shift
        # (3) "sub+int": ambos
        f0 = min(sw, key=lambda t: abs(t[0][0]) + abs(t[0][1]))[1]
        i_int = iou_shift(f0["pix"], B)
        sub0 = max(((float((_np_lit(f["pix"]) & B).sum() / (_np_lit(f["pix"]) | B).sum()), w, f) for w, f in sw),
                   key=lambda t: t[0])
        subI = None
        for w, f in sw:
            v = iou_shift(f["pix"], B)
            if subI is None or v[0] > subI[0][0] + 1e-12:
                subI = (v, w, f)
        f = sub0[2]
        rows.append(dict(face=name, iou_int=i_int[0], shift_int=i_int[1:], iou_sub0=sub0[0], wiggle_sub0=sub0[1],
                         iou_subint=subI[0][0], shift_subint=subI[0][1:], wiggle_subint=subI[1],
                         leds_host=int(_np_lit(f["pix"]).sum()), leds_real=int(B.sum()),
                         color_host=mean_color(f["pix"]), color_real=mean_color(ref),
                         weights={k: round(v, 3) for k, v in f["weights"].items() if abs(v) > 1e-3}))
        save_txt(os.path.join(a.out, f"val_{name}.txt"), f["pix"])
        A = _np_lit(f["pix"])
        diff = [[(60, 60, 60) if A[y, x] and B[y, x] else ((255, 60, 60) if A[y, x] else ((60, 160, 255) if B[y, x] else (0, 0, 0)))
                 for x in range(W)] for y in range(H)]
        panels += [to_pil(led_render(ref, s=4, gap=1, sep=12)), to_pil(led_render(f["pix"], s=4, gap=1, sep=12)),
                   to_pil(led_render(diff, s=4, gap=1, sep=12))]
        labels += [f"{name} captura real (cara{nn})",
                   f"{name} hostrender  IoU={sub0[0]:.3f} wiggle=({sub0[1][0]},{sub0[1][1]})u shift 0",
                   f"{name} diff: gris=ambos, rojo=solo host, azul=solo real"]
    label_strip(panels, labels).save(os.path.join(a.out, "validacion.png"))
    print(f"{'cara':9s} {'IoU sub0':>9s} {'wiggle (u)':>15s} {'IoU int':>8s} {'shift':>8s} {'LEDs h/r':>9s}  color medio host vs real")
    for d in rows:
        print(f"{d['face']:9s} {d['iou_sub0']:9.3f} {str(d['wiggle_sub0']):>15s} {d['iou_int']:8.3f} {str(d['shift_int']):>8s} "
              f"{d['leds_host']:>4d}/{d['leds_real']:<4d}  {d['color_host']} vs {d['color_real']}")
    print("IoU sub0 = wiggle subpixel en el rango del firmware (±2 u), sin shift entero")
    print("IoU int  = wiggle 0 + mejor shift entero ±3 px (no modela el wiggle subpixel)")
    print("→", os.path.join(a.out, "validacion.png"))
    if a.json:
        json.dump(rows, open(a.json, "w"), indent=1, default=list)


def cmd_verts(a):
    """Vértices tras asentar la receta: modelo base, modelo con morphs, y pixel (col, renglón)
    del panel normal con wiggle 0. Col/renglón enteros = centro de un LED."""
    r = load_recipe(a.recipe)
    s = settings_of(r, {"facesize": a.facesize, "wiggle": "off"})
    V = _verts_run(r, s, a.frames)
    bv = base_vertices()
    print(f"# {r['name']}  facesize {s['facesize']}  (col 0..63 izq→der, renglón 0..31 arriba→abajo)")
    print(f"{'v':>3s} {'base x':>8s} {'y':>8s} {'z':>8s} | {'morph x':>8s} {'y':>8s} {'z':>8s} | {'col':>6s} {'renglón':>7s}")
    for i, row in enumerate(V["pM"]):
        print(f"{i:3d} {bv[i][0]:8.2f} {bv[i][1]:8.2f} {bv[i][2]:8.2f} | {row[5]:8.2f} {row[6]:8.2f} {row[7]:8.2f} | {row[3]:6.2f} {row[4]:7.2f}")
    for name, arr in V.items():
        if name == "pM":
            continue
        print(f"# objeto {name}")
        for i, row in enumerate(arr):
            print(f"{i:3d} {'':8s} {'':8s} {'':8s} | {row[5]:8.2f} {row[6]:8.2f} {row[7]:8.2f} | {row[3]:6.2f} {row[4]:7.2f}")


def base_triangles():
    import re
    p = NUKUDE_H
    txt = open(p).read()
    blk = txt[txt.index("basisIndexes[44]"):]
    blk = blk[:blk.index("};")]
    return [tuple(int(v) for v in t) for t in re.findall(r"IndexGroup\((\d+),(\d+),(\d+)\)", blk)]


def cmd_meshmap(a):
    """PNG del panel normal con la malla encima: LEDs, triángulos y número de vértice."""
    from PIL import Image, ImageDraw
    r = load_recipe(a.recipe)
    s = settings_of(r, {"facesize": a.facesize, "wiggle": "off"})
    res, _ = simulate([(r, a.frames)], s)
    pix = res[-1][1]["pix"]
    V = _verts_run(r, s, a.frames)
    k = a.scale
    im = Image.new("RGB", (W * k, H * k), (10, 10, 10))
    d = ImageDraw.Draw(im)
    for y in range(H):
        for x in range(W):
            c = pix[y][x]
            c = tuple(int(v * 0.45) for v in c) if any(c) else (26, 26, 26)
            d.ellipse([x * k + 2, y * k + 2, x * k + k - 3, y * k + k - 3], fill=c)
    P = lambda i, arr: (arr[i, 3] * k + k / 2, arr[i, 4] * k + k / 2)
    colors = {"ojo": (255, 210, 60), "boca": (90, 200, 255), "nariz": (255, 120, 200), "chapitas": (150, 255, 120)}
    def part(t):
        if all(1 <= v <= 15 for v in t): return "ojo"
        if all(41 <= v <= 52 for v in t): return "chapitas"
        if all(v == 0 or 22 <= v <= 28 for v in t): return "nariz"
        return "boca"
    for t in base_triangles():
        pts = [P(i, V["pM"]) for i in t] + [P(t[0], V["pM"])]
        d.line(pts, fill=colors[part(t)], width=1)
    for name, arr in V.items():
        if name == "pM":
            continue
        for i in range(len(arr)):
            x, y = P(i, arr)
            d.rectangle([x - 2, y - 2, x + 2, y + 2], outline=(255, 255, 255))
    seen = {}
    for i in range(len(V["pM"])):
        x, y = P(i, V["pM"])
        key = (round(x / 6), round(y / 6))
        off = seen.get(key, 0); seen[key] = off + 1
        d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(255, 255, 255))
        d.text((x + 3, y - 10 + off * 10), str(i), fill=(255, 255, 255), stroke_width=2, stroke_fill=(0, 0, 0))
    for gx in range(0, W, 8):
        d.text((gx * k + 2, H * k - 12), str(gx), fill=(120, 120, 120))
    for gy in range(0, H, 4):
        d.text((2, gy * k + 2), str(gy), fill=(120, 120, 120))
    out = os.path.join(a.out, f"malla_{r['name']}.png")
    os.makedirs(a.out, exist_ok=True)
    im.save(out)
    print("→", out)


def cmd_export(a):
    """C++ listo pa pegar: morphs (deltas ya resueltos) y objetos (vértices de modelo)."""
    d, ddir = _load(a.file, os.getcwd())
    is_morph = "name" in d and any(k in d for k in ("deltas", "positions", "pixel_positions")) and "morphs" not in d
    if is_morph:
        d = {"name": d["name"] + "_export", "base": a.base, "morphs": [d], "weights": {d["name"]: 1.0}}
    d["_dir"] = ddir
    r = load_recipe(d)
    r["_dir"] = ddir
    ms = morph_defs(r)
    if r.get("_solve_err_px") is not None:
        print(f"// pixel_positions resueltas contra el motor (error máx {r['_solve_err_px']:.3f} px, facesize {settings_of(r)['facesize']})")
    for m in ms:
        nm, n = m["name"], len(m["indices"])
        print(f"// --- morph {nm} ({n} vértices)")
        print(f"// NukudeFlat.h (o una copia): 1) '{nm}' al final del enum Morphs  2) morphCount y morphs[] +1")
        print(f"// 3) estos arrays  4) Morph({n}, {nm}Indexes, {nm}Vectors) al final de morphs[]")
        print(f"// 5) LinkControlParameters(): AddParameter(NukudeFace::{nm}, pM.GetMorphWeightReference(NukudeFace::{nm}), {m['frames']});")
        print(f"int {nm}Indexes[{n}] = {{{','.join(str(i) for i in m['indices'])}}};")
        print(f"Vector3D {nm}Vectors[{n}] = {{" + ",".join(f"Vector3D({x:.4f}f,{y:.4f}f,{z:.4f}f)" for x, y, z in m["deltas"]) + "};")
    for o in object_defs(r):
        nv, nt = len(o["vertices"]), len(o["triangles"])
        print(f"// --- objeto {o['name']} ({nv} vértices, {nt} triángulos, espacio {o.get('space', 'model')})")
        print(f"Vector3D {o['name']}Vertices[{nv}] = {{" + ",".join(f"Vector3D({x:.4f}f,{y:.4f}f,{z:.4f}f)" for x, y, z in o["vertices"]) + "};")
        print(f"IndexGroup {o['name']}Indexes[{nt}] = {{" + ",".join(f"IndexGroup({t[0]},{t[1]},{t[2]})" for t in o["triangles"]) + "};")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--facesize", type=int, default=None, help="Menu FaceSize 0-10 (default receta/9)")
        p.add_argument("--wiggle", default=None, help="off | auto | x,y (unidades de cámara, ±2)")
        p.add_argument("--out", default=os.path.join(HERE, "out"))
        p.add_argument("--warm-frames", type=int, default=250, help="frames de la cara previa antes de cambiar")

    p = sp.add_parser("render"); common(p)
    p.add_argument("recipes", nargs="+")
    p.add_argument("--frames", type=int, default=int(1.5 * FPS), help="frames tras cambiar (default 1.5 s)")
    p.add_argument("--warmup", default=None, help="cara previa (default DEFAULT)")
    p.set_defaults(fn=cmd_render)

    p = sp.add_parser("transition"); common(p)
    p.add_argument("a"); p.add_argument("b")
    p.add_argument("--frames", type=int, default=50, help="frames de transición (125 fps)")
    p.add_argument("--every", type=int, default=5, help="guarda 1 de cada N frames")
    p.add_argument("--name", default=None)
    p.set_defaults(fn=cmd_transition)

    p = sp.add_parser("validate"); common(p)
    p.add_argument("faces", nargs="*")
    p.add_argument("--step", type=float, default=0.25, help="paso del barrido de wiggle (unidades de cámara)")
    p.add_argument("--json", default=None)
    p.set_defaults(fn=cmd_validate)

    p = sp.add_parser("verts"); common(p)
    p.add_argument("recipe")
    p.add_argument("--frames", type=int, default=int(1.5 * FPS))
    p.set_defaults(fn=cmd_verts)

    p = sp.add_parser("meshmap"); common(p)
    p.add_argument("recipe")
    p.add_argument("--frames", type=int, default=int(1.5 * FPS))
    p.add_argument("--scale", type=int, default=20, help="pixeles por LED")
    p.set_defaults(fn=cmd_meshmap)

    p = sp.add_parser("export")
    p.add_argument("file", help="morph .json o receta .json")
    p.add_argument("--base", default="DEFAULT", help="cara de contexto pa resolver pixel_positions de un morph suelto")
    p.set_defaults(fn=cmd_export)

    a = ap.parse_args()
    if getattr(a, "wiggle", None) and a.wiggle not in ("off", "auto"):
        a.wiggle = [float(v) for v in a.wiggle.split(",")]
    a.fn(a)


if __name__ == "__main__":
    main()
