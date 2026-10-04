#!/usr/bin/env python3
"""Capturas de la cara del protogen por serial (comando 'd' del firmware) → PNG.

  ./captura.py                      captura la cara actual
  ./captura.py 0 5 16               captura esas caras (las pone con f<N>)
  ./captura.py --todas              todas las caras del menú
  ./captura.py 16 --frames 6 --cada 0.15   varias capturas seguidas (animaciones)
  ./captura.py 6 --vista            lo que REALMENTE va a los paneles, de frente (comando 'v');
                                    sin --vista es la cámara 64x32 + su espejo (comando 'd')
  ./captura.py --boot --frames 14 --cada 0.3   repite el arranque (B) y lo captura
  ./captura.py --oled               la pantallita OLED de adentro (comando 'O'), como se ve
  ./captura.py 17 --oled            pone la cara 17 y captura la OLED
  ./captura.py --oled --temp 85 --frames 4 --cada 0.2   finge 85 °C (w85) pa ver el foco
                                    de temperatura parpadear; al final regresa al sensor (w0)
  ./captura.py --gesto 1            dedo de mentira (g1 = boop + boooop) y graba la OLED mientras:
                                    cada frame DISTINTO a PNG + una tira con el tiempo desde g1
                                    (2 sencillo, 3 doble_tap, 4 suelta, 5 abrazo, 6 seguido; los
                                    patrones son los de tools/oledsim/boop_presets.json). El FIRE
                                    cambia la cara: al final regresa la que tenía (f<N>)

Mientras captura baja el brillo al mínimo (b0) pa no deslumbrar a nadie (con --oled no
lo toca, salvo --brillo), y al final regresa la cara y el brillo que tenía. Sólo stdlib
(corre en el Pi pelón). Salida: ~/capturas/<fecha>/cara<N>_<k>.png (panel "normal" +
su espejo, estilo LED) y el .txt crudo con el hex; con --oled, oled_cara<N>_<k>.png
(128x64 a 6x, pixeles con su brillito, como el simulador) y su .txt; con --gesto,
gesto<N>_<k>_<ms>ms.png/.txt por frame distinto y gesto<N>_tira.png.
"""
import argparse, glob, json, os, re, select, struct, sys, termios, time, zlib

# el symlink estable del Teensy (ttyACM* cambia de número si algo se quedó con el viejo abierto)
PORT_GLOB = "/dev/serial/by-id/usb-Teensyduino_USB_Serial_*"
W, H = 64, 32
OLED_W, OLED_H = 128, 64


def puertos():
    """Los Teensys conectados (rutas de /dev/serial/by-id)."""
    return sorted(glob.glob(PORT_GLOB))


def puerto():
    """El único Teensy conectado; si no hay o hay más de uno, sale con el error."""
    ps = puertos()
    if len(ps) == 1:
        return ps[0]
    if not ps:
        sys.exit(f"no encuentro el Teensy ({PORT_GLOB}): ¿está conectado? (o dalo con --puerto)")
    sys.exit(f"hay {len(ps)} Teensys conectados: {' '.join(ps)} (elige uno con --puerto)")


class Teensy:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0          # raw: sin eco ni traducciones
        a[2] |= termios.CREAD | termios.CLOCAL
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        self.buf = b""

    def send(self, s):
        os.write(self.fd, s.encode())

    def lines(self, timeout):
        end = time.time() + timeout
        while True:
            # primero lo que ya llegó: un dump chico (O) puede caer completo en un solo read
            while b"\n" in self.buf:
                ln, self.buf = self.buf.split(b"\n", 1)
                yield ln.decode(errors="replace").strip()
            if time.time() >= end:
                return
            r, _, _ = select.select([self.fd], [], [], max(0.0, end - time.time()))
            if r:
                self.buf += os.read(self.fd, 65536)

    def wait_for(self, pred, timeout):
        for ln in self.lines(timeout):
            if pred(ln):
                return ln
        return None

    def drain(self):
        for _ in self.lines(0.05):
            pass
        self.buf = b""

    def status(self):
        self.drain()
        self.send("s\n")
        ln = self.wait_for(lambda l: l.startswith("S face="), 2)
        if not ln:
            sys.exit("no contestó el estado (¿firmware sin comandos?)")
        return dict(re.findall(r"(\w+)=(\d+)", ln)), ln

    def cmd(self, c, n):
        self.send(f"{c}{n}\n")
        ok = self.wait_for(lambda l: l.startswith(("OK ", "ERR ")), 2)
        if not ok or ok.startswith("ERR"):
            raise RuntimeError(f"{c}{n} → {ok}")

    def dump_front(self):
        """Lo que realmente va a los paneles, de frente: 32 renglones x 128 (izq | der)."""
        self.drain()
        self.send("v\n")
        if not self.wait_for(lambda l: l.startswith("VIEW face="), 3):
            raise RuntimeError("no llegó VIEW (¿firmware sin el comando v?)")
        rows = {}
        for ln in self.lines(3):
            if ln == "VIEW END":
                break
            m = re.match(r"V(\d\d) ([0-9A-F]{768})$", ln)
            if m:
                rows[int(m.group(1))] = m.group(2)
        if len(rows) != H:
            raise RuntimeError(f"vista incompleta: {len(rows)} renglones")
        return [[tuple(int(r[x * 6 + k * 2: x * 6 + k * 2 + 2], 16) for k in range(3))
                 for x in range(2 * W)] for r in (rows[i] for i in range(H))]

    def dump_oled(self, drain=True, otras=None):
        """La OLED de adentro: (pixeles 64x128 con 1 = prendido, contraste, encabezado, renglones).
        drain=False no tira lo que ya venía (ráfaga de --gesto); las líneas que no son del dump
        (p. ej. "[boop] FIRE ...") se agregan a la lista otras."""
        if drain:
            self.drain()
        self.send("O\n")
        hdr = None
        for ln in self.lines(3):
            if ln.startswith("OLED "):
                hdr = ln
                break
            if otras is not None and ln:
                otras.append(ln)
        if not hdr:
            raise RuntimeError("no llegó OLED (¿firmware sin el comando O?)")
        if not hdr.startswith("OLED inv="):
            raise RuntimeError(f"la OLED no está: {hdr}")
        info = dict(re.findall(r"(\w+)=(\d+)", hdr))
        pages = {}
        for ln in self.lines(3):
            if ln == "OLED END":
                break
            m = re.match(r"L(\d\d) ([0-9A-F]{256})$", ln)
            if m:
                pages[int(m.group(1))] = bytes.fromhex(m.group(2))
            elif otras is not None and ln:
                otras.append(ln)
        if sorted(pages) != list(range(OLED_H // 8)):
            raise RuntimeError(f"OLED incompleta: {len(pages)} páginas")
        inv = info.get("inv") == "1"
        px = [[((pages[y >> 3][x] >> (y & 7)) & 1) ^ inv for x in range(OLED_W)] for y in range(OLED_H)]
        raw = [f"L{p:02d} {pages[p].hex().upper()}" for p in range(OLED_H // 8)]
        return px, int(info.get("contrast", 207)), hdr, raw

    def dump(self):
        self.drain()
        self.send("d\n")
        if not self.wait_for(lambda l: l.startswith("DUMP face="), 3):
            raise RuntimeError("no llegó DUMP")
        rows = {}
        for ln in self.lines(3):
            if ln == "DUMP END":
                break
            m = re.match(r"D(\d\d) ([0-9A-F]{384})$", ln)
            if m:
                rows[int(m.group(1))] = m.group(2)
        if len(rows) != H:
            raise RuntimeError(f"dump incompleto: {len(rows)} renglones")
        return [[tuple(int(r[x * 6 + k * 2: x * 6 + k * 2 + 2], 16) for k in range(3))
                 for x in range(W)] for r in (rows[i] for i in range(H))]


def png_bytes(path, w, h, rows):
    """rows = h bytearrays de w*3 bytes RGB."""
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def oled_render(px, contrast=207, s=6, tint=(222, 238, 255)):
    """Como el simulador (oledlook.py): cada pixel es un puntito de (s-1)x(s-1) con 1 de
    separación y esquinitas suaves, el brillo sigue al contraste (0xCF = todo, 0 = ~45%) y
    los prendidos dejan un halo tenue alrededor. Regresa (ancho, alto, renglones RGB)."""
    off, bezel = (7, 9, 12), (24, 27, 32)
    k = min(1.0, max(0.45, 0.45 + 0.55 * contrast / 207.0))
    lit = tuple(c * k for c in tint)
    mix = lambda a: bytes(int(min(255, o + a * (l - o))) for o, l in zip(off, lit))
    glow_w = ((1, 2, 1), (2, 4, 2), (1, 2, 1))
    m, dot = 2 * s, s - 1
    W, H = OLED_W * s + 2 * m, OLED_H * s + 2 * m
    blank = bytes(off) * W
    rows = [bytearray(blank) for _ in range(H)]
    full, corner = mix(1.0), mix(0.55)
    for y in range(OLED_H):
        for x in range(OLED_W):
            g = 0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < OLED_H and 0 <= xx < OLED_W and px[yy][xx]:
                        g += glow_w[dy + 1][dx + 1]
            if not g:
                continue
            halo = mix(0.30 * g / 16)
            on = px[y][x]
            for j in range(s):
                row = rows[m + y * s + j]
                for i in range(s):
                    if on and i < dot and j < dot:
                        c = corner if (i in (0, dot - 1) and j in (0, dot - 1)) else full
                    else:
                        c = halo
                    o = (m + x * s + i) * 3
                    row[o:o + 3] = c
    x0, y0, x1, y1 = m - 3, m - 3, m + OLED_W * s + 1, m + OLED_H * s + 1  # marco del cristal
    for x in range(x0, x1 + 1):
        rows[y0][x * 3:x * 3 + 3] = bytes(bezel)
        rows[y1][x * 3:x * 3 + 3] = bytes(bezel)
    for y in range(y0, y1 + 1):
        rows[y][x0 * 3:x0 * 3 + 3] = bytes(bezel)
        rows[y][x1 * 3:x1 * 3 + 3] = bytes(bezel)
    return W, H, rows


def png(path, pix):
    h, w = len(pix), len(pix[0])
    raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in pix)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def led_render(img, s=8, gap=2, sep=24):
    """Los dos paneles lado a lado como LEDs. img de 64 = cámara (se arma
    [espejo | normal]); img de 128 = vista de frente ya armada (--vista)."""
    if len(img[0]) == 2 * W:
        mir, img = [row[:W] for row in img], [row[W:] for row in img]
    else:
        mir = [row[::-1] for row in img]
    out_w = 2 * W * s + sep
    out = [[(12, 12, 12)] * out_w for _ in range(H * s)]
    for panel, ox in ((mir, 0), (img, W * s + sep)):
        for y in range(H):
            for x in range(W):
                c = panel[y][x]
                c = c if any(c) else (28, 28, 28)  # LED apagado se ve gris oscuro
                for dy in range(s - gap):
                    row = out[y * s + dy]
                    for dx in range(s - gap):
                        row[ox + x * s + dx] = c
    return out


# Letras de 3x5 pa los letreros de la tira (sólo stdlib: nada de PIL en el Pi)
FONT = {c: g.split("/") for c, g in {
    "0": "###/#.#/#.#/#.#/###", "1": ".#./##./.#./.#./###", "2": "###/..#/###/#../###",
    "3": "###/..#/.##/..#/###", "4": "#.#/#.#/###/..#/..#", "5": "###/#../###/..#/###",
    "6": "###/#../###/#.#/###", "7": "###/..#/.#./.#./.#.", "8": "###/#.#/###/#.#/###",
    "9": "###/#.#/###/..#/###", "A": ".#./#.#/###/#.#/#.#", "B": "##./#.#/##./#.#/##.",
    "C": ".##/#../#../#../.##", "D": "##./#.#/#.#/#.#/##.", "E": "###/#../##./#../###",
    "F": "###/#../##./#../#..", "G": ".##/#../#.#/#.#/.##", "H": "#.#/#.#/###/#.#/#.#",
    "I": "###/.#./.#./.#./###", "J": "..#/..#/..#/#.#/.#.", "K": "#.#/#.#/##./#.#/#.#",
    "L": "#../#../#../#../###", "M": "#.#/###/###/#.#/#.#", "N": "##./#.#/#.#/#.#/#.#",
    "O": ".#./#.#/#.#/#.#/.#.", "P": "##./#.#/##./#../#..", "Q": ".#./#.#/#.#/##./.##",
    "R": "##./#.#/##./#.#/#.#", "S": ".##/#../.#./..#/##.", "T": "###/.#./.#./.#./.#.",
    "U": "#.#/#.#/#.#/#.#/###", "V": "#.#/#.#/#.#/#.#/.#.", "W": "#.#/#.#/###/###/#.#",
    "X": "#.#/#.#/.#./#.#/#.#", "Y": "#.#/#.#/.#./.#./.#.", "Z": "###/..#/.#./#../###",
    "+": ".../.#./###/.#./...", "-": ".../.../###/.../...", ":": ".../.#./.../.#./...",
    ".": ".../.../.../.../.#.", " ": ".../.../.../.../...",
}.items()}


def texto(rows, x, y, s, k=3, color=(230, 230, 230)):
    """Escribe s en rows (bytearrays RGB) con la letra de 3x5 a escala k."""
    for ch in s.upper():
        g = FONT.get(ch, FONT[" "])
        for j, fila in enumerate(g):
            for i, c in enumerate(fila):
                if c != "#":
                    continue
                for dy in range(k):
                    row = rows[y + j * k + dy]
                    for dx in range(k):
                        o = (x + i * k + dx) * 3
                        if 0 <= o < len(row) - 2:
                            row[o:o + 3] = bytes(color)
        x += 4 * k


def tira(path, tiles, cols=4, s=3):
    """tiles = [(px, contraste, renglón 1, renglón 2)] -> PNG con cada frame y su letrero abajo."""
    tw, th = OLED_W * s + 4 * s, OLED_H * s + 4 * s   # lo que da oled_render
    lab, gap = 44, 8
    cols = max(1, min(cols, len(tiles)))
    nrows = (len(tiles) + cols - 1) // cols
    W, H = cols * (tw + gap) + gap, nrows * (th + lab + gap) + gap
    bg = bytes((16, 17, 20))
    rows = [bytearray(bg * W) for _ in range(H)]
    for i, (px, contraste, l1, l2) in enumerate(tiles):
        x0, y0 = gap + (i % cols) * (tw + gap), gap + (i // cols) * (th + lab + gap)
        _, _, img = oled_render(px, contraste, s)
        for j, r in enumerate(img):
            rows[y0 + j][x0 * 3:(x0 + tw) * 3] = r
        texto(rows, x0 + 2, y0 + th + 6, l1)
        texto(rows, x0 + 2, y0 + th + 25, l2, color=(255, 200, 90))
    png_bytes(path, W, H, rows)


def preset_info(n):
    """(nombre, segundos que dura g<N>) de tools/oledsim/boop_presets.json si está a la mano:
    medio segundo sin boop + el patrón + 1 s de cola, como BoopScript.h."""
    p = os.path.join(os.path.dirname(os.path.realpath(__file__)), "oledsim", "boop_presets.json")
    try:
        with open(p) as f:
            for name, pr in json.load(f)["presets"].items():
                if int(pr.get("g", 0)) == n:
                    return name, 0.5 + sum(pr["pattern"]) / 1000.0 + 1.0
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return f"g{n}", None


def captura_gesto(t, n, out, durante=None, cada=0.06):
    """g<N> y luego 'O' tan seguido como dé el cable (~cada 60-100 ms): guarda cada frame
    DISTINTO (pixeles o contraste) y la tira con el tiempo desde g<N> (cuando se leyó)."""
    nombre, dura = preset_info(n)
    if durante is None:
        durante = max(3.5, dura or 3.5)
    t.drain()
    t.send(f"s\ng{n}\n")  # el Teensy corre los dos en el mismo frame: el t= del estado = arranque del g
    st = t.wait_for(lambda l: l.startswith("S face="), 2)
    ok = t.wait_for(lambda l: l.startswith(("OK g", "ERR g")), 2)
    if not st or not ok:
        raise RuntimeError("no contestó s/g (¿firmware sin el comando g?)")
    if ok.startswith("ERR"):
        raise RuntimeError(f"{ok}: N fuera de 0-6, el boop apagado en el menú o la cabeza va arrancando")
    t0 = int(re.search(r"\bt=(\d+)", st).group(1))
    otras, frames, lecturas = [], [], 0
    fin = time.time() + durante
    while time.time() < fin:
        tic = time.time()
        px, contraste, hdr, raw = t.dump_oled(drain=False, otras=otras)
        lecturas += 1
        ms = int(re.search(r"\bt=(\d+)", hdr).group(1)) - t0
        if not frames or (px, contraste) != (frames[-1][1], frames[-1][2]):
            frames.append((ms, px, contraste, hdr, raw))
        time.sleep(max(0.0, cada - (time.time() - tic)))
    eventos = []
    for ln in otras:
        m = re.match(r"\[boop\] (\S+) .*\bt=(\d+)", ln)
        if m:
            eventos.append((int(m.group(2)) - t0, m.group(1)))
    tiles = []
    for k, (ms, px, contraste, hdr, raw) in enumerate(frames):
        base = os.path.join(out, f"gesto{n}_{k:02d}_{ms:05d}ms")
        with open(base + ".txt", "w") as f:
            f.write(hdr + "\n" + "\n".join(raw) + "\n")
        png_bytes(base + ".png", *oled_render(px, contraste))
        ev = [e for e in eventos if e[0] <= ms]
        tiles.append((px, contraste, f"+{ms} MS", ev[-1][1] if ev else ""))
    path = os.path.join(out, f"gesto{n}_tira.png")
    tira(path, tiles)
    print(f"gesto {n} ({nombre}): {lecturas} lecturas en {durante:.1f} s, {len(frames)} frames distintos → {path}")
    print("  eventos: " + (", ".join(f"{name} +{ms}" for ms, name in eventos) or "ninguno"))
    return frames, eventos


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("caras", nargs="*", type=int)
    ap.add_argument("--todas", action="store_true")
    ap.add_argument("--frames", type=int, default=1)
    ap.add_argument("--cada", type=float, default=0.2, help="segundos entre frames")
    ap.add_argument("--asentar", type=float, default=1.5, help="espera tras cambiar de cara (morph)")
    ap.add_argument("--brillo", type=int, default=None, help="brillo mientras captura (0-9; default 0, con --oled no se toca)")
    ap.add_argument("--vista", action="store_true", help="lo que va a los paneles (v) en vez de la cámara (d)")
    ap.add_argument("--boot", action="store_true", help="repite el arranque (B) y lo captura (implica --vista)")
    ap.add_argument("--oled", action="store_true", help="captura la pantallita OLED de adentro (O) en vez de la cara")
    ap.add_argument("--temp", type=int, default=None, help="finge esta temperatura (w<N>) mientras captura; al final w0")
    ap.add_argument("--gesto", type=int, default=None, help="dedo de mentira g<N> (1 doble, 2 sencillo, 3 doble_tap, 4 suelta, 5 abrazo, 6 seguido) y graba la OLED (implica --oled)")
    ap.add_argument("--durante", type=float, default=None, help="segundos grabando con --gesto (default 3.5, más si el patrón es largo)")
    ap.add_argument("--out", default=os.path.expanduser("~/capturas/" + time.strftime("%F-%H%M")))
    ap.add_argument("--puerto", default=None, help="puerto serial del Teensy (default: el único en /dev/serial/by-id)")
    a = ap.parse_args()
    if a.boot:
        a.vista, a.caras, a.todas = True, [], False
    if a.gesto is not None:
        a.oled = True
    if a.oled:
        a.vista = False
    if a.brillo is None:
        a.brillo = None if a.oled else 0

    t = Teensy(a.puerto or puerto())
    st, ln = t.status()
    print(ln)
    cara0, brillo0 = int(st["face"]), int(st["bright"])
    if a.brillo is None:
        a.brillo = brillo0
    apagada = a.vista and st.get("off") == "1"  # con 'o0' la vista sale negra; la cámara no
    caras = a.caras
    if a.todas:
        caras = list(range(40))  # el firmware rechaza las que no existen
    os.makedirs(a.out, exist_ok=True)
    try:
        if a.brillo != brillo0:
            t.cmd("b", a.brillo)
        if apagada:
            t.cmd("o", 1)
        if a.temp is not None:
            t.cmd("w", a.temp)
            time.sleep(0.5)  # la OLED lee la temperatura 5 veces por segundo
        for n in caras or [cara0]:
            if caras:
                try:
                    t.cmd("f", n)
                except RuntimeError:
                    if a.todas:
                        break
                    raise
                time.sleep(a.asentar)
            if a.boot:
                t.send("B\n")
                time.sleep(0.05)
            if a.gesto is not None:
                captura_gesto(t, a.gesto, a.out, a.durante)
                continue
            t0 = time.time()
            for k in range(a.frames):
                if a.oled:
                    px, contraste, hdr, raw = t.dump_oled()
                    base = os.path.join(a.out, f"oled_cara{n:02d}_{k}")
                    with open(base + ".txt", "w") as f:
                        f.write(hdr + "\n" + "\n".join(raw) + "\n")
                    png_bytes(base + ".png", *oled_render(px, contraste))
                    lit = sum(map(sum, px))
                    print(f"OLED cara {n} frame {k}: {lit} pixeles prendidos, contraste {contraste} → {base}.png")
                    if k + 1 < a.frames:
                        time.sleep(a.cada)
                    continue
                img = t.dump_front() if a.vista else t.dump()
                base = os.path.join(a.out, f"boot_{int((time.time() - t0) * 1000):04d}ms" if a.boot
                                    else f"cara{n:02d}_{k}{'_vista' if a.vista else ''}")
                with open(base + ".txt", "w") as f:
                    f.write("\n".join("".join("%02X%02X%02X" % p for p in row) for row in img) + "\n")
                png(base + ".png", led_render(img))
                lit = sum(1 for row in img for p in row if any(p))
                print(f"cara {n} frame {k}: {lit} LEDs prendidos → {base}.png")
                if k + 1 < a.frames:
                    time.sleep(a.cada)
    finally:
        if a.gesto is not None:  # por si se cortó a media jugada (ERR con el sensor apagado: nada que parar)
            t.send("g0\n")
            t.wait_for(lambda l: l.startswith(("OK g0", "ERR g0")), 2)
        if a.temp is not None:
            t.cmd("w", 0)
        t.cmd("f", cara0)
        if a.brillo != brillo0:
            t.cmd("b", brillo0)
        if apagada:
            t.cmd("o", 0)
        print("restaurado:", t.status()[1])


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as e:
        sys.exit(f"error: {e}")
