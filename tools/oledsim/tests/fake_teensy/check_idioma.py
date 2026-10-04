#!/usr/bin/env python3
"""l<N> contra la cabeza de mentira: OK/ERR como el firmware, el estado trae lang=<N>, la
pantallita cambia de idioma al momento y l255 la regresa al idioma del build (igualita a como
arrancó; luego l0 la deja en español).
    check_idioma.py <pty> [--eeprom ARCHIVO]
        la cabeza recién prendida con la EEPROM virgen: su lang= es el del build (IDIOMA_DEFECTO)
    check_idioma.py <pty> --espera N [--manda lM] [--eeprom ARCHIVO]
        nomás checa que esté en el idioma N (tras un reinicio: lo que leyó de la EEPROM 201) y
        luego manda lM
Con --eeprom (el archivo de fake_teensy --eeprom) también checa el byte 201 tras cada l<N>.
Sale 0 si todo cuadra."""
import argparse
import os
import select
import sys
import time
import tty

ap = argparse.ArgumentParser()
ap.add_argument("pty")
ap.add_argument("--eeprom")
ap.add_argument("--espera", type=int)
ap.add_argument("--manda")
args = ap.parse_args()

fd = os.open(args.pty, os.O_RDWR | os.O_NOCTTY)
tty.setraw(fd)
EEPROM_IDIOMA = 201


def talk(cmd, wait=0.5):
    os.write(fd, (cmd + "\n").encode())
    t0, buf = time.time(), b""
    while time.time() - t0 < wait:
        if select.select([fd], [], [], 0.05)[0]:
            buf += os.read(fd, 65536)
    return buf.decode(errors="replace").splitlines()


def oled():
    rows = [ln for ln in talk("O", 1.0) if ln.startswith("L")]
    return rows if len(rows) == 8 else None


def answer(cmd):
    return next((ln for ln in talk(cmd) if ln.startswith(("OK ", "ERR "))), None)


def lang():
    st = next((ln for ln in talk("s") if ln.startswith("S face=")), "")
    return next((kv.split("=")[1] for kv in st.split() if kv.startswith("lang=")), None)


fail = []


def eeprom(cmd, want):
    """byte 201 del archivo de la EEPROM falsa (sin archivo = virgen, 0xFF)"""
    if not args.eeprom:
        return
    got = 0xFF
    if os.path.exists(args.eeprom):
        with open(args.eeprom, "rb") as f:
            got = f.read()[EEPROM_IDIOMA]
    if got != want:
        fail.append(f"{cmd}: EEPROM 201 = {got} (quería {want})")


def manda(cmd, n):
    """lN que debe dar OK, dejar lang=N (255: el del build) y la EEPROM 201 en N"""
    got = answer(cmd)
    if got != "OK " + cmd:
        fail.append(f"{cmd}: {got!r} (quería 'OK {cmd}')")
    eeprom(cmd, n)


if args.espera is not None:
    if lang() != str(args.espera):
        fail.append(f"lang={lang()} (quería {args.espera})")
    if args.manda:
        manda(args.manda, int(args.manda[1:]))
    nombre = f"lang={args.espera}" + (f", {args.manda}" if args.manda else "")
    print(f"l<N> ({nombre}): " + ("todo bien" if not fail else "FALLAS: " + "; ".join(fail)))
    sys.exit(1 if fail else 0)

defecto = lang()   # EEPROM virgen: el idioma del build
antes = oled()
if antes is None:
    fail.append("O: no salió la captura de la OLED")
if defecto not in ("0", "1", "2"):
    fail.append(f"estado: lang={defecto} recién prendida")
eeprom("arranque", 0xFF)
for cmd in ("l3", "l254", "l256"):   # sólo 0..2 y 255
    got = answer(cmd)
    if got != "ERR " + cmd:
        fail.append(f"{cmd}: {got!r} (quería 'ERR {cmd}')")
eeprom("l3/l254/l256", 0xFF)
manda("l1", 1)
manda("l2", 2)
if lang() != "2":
    fail.append(f"estado: lang={lang()} después de l2")
time.sleep(0.5)
otro = oled()
if antes and defecto != "2" and otro == antes:
    fail.append("l2: la OLED no cambió")
manda("l255", 0xFF)
if lang() != defecto:
    fail.append(f"l255: lang={lang()} (quería el del build, {defecto})")
time.sleep(0.5)
if antes and oled() != antes:
    fail.append("l255: la OLED no quedó igual que al prender")
manda("l0", 0)
if lang() != "0":
    fail.append("l0: no quedó en español")

print(f"l<N> (build lang={defecto}): " + ("todo bien" if not fail else "FALLAS: " + "; ".join(fail)))
sys.exit(1 if fail else 0)
