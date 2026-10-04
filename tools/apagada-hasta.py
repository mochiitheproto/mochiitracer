#!/usr/bin/env python3
"""Apaga la pantalla de la cabeza ('o0') y la mantiene apagada hasta HH:MM; a esa
hora la prende ('o1'). El apagado vive en RAM, así que si el Teensy se reinicia
regresa prendido: por eso cada 5 min revisa y lo vuelve a apagar.

  ./apagada-hasta.py 08:00
  sudo systemd-run --uid=$USER --unit=cabeza-apagada "$(realpath tools/apagada-hasta.py)" 08:00

Usa el Teensy que esté conectado (el único en /dev/serial/by-id, igual que captura.py).
"""
import datetime, os, sys, time

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
import captura as c

CADA = 300


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def poner(off):
    """Pone o0/o1 si hace falta. Regresa True si la cabeza contestó."""
    ps = c.puertos()
    if len(ps) != 1:
        log("sin puerto:", "no hay Teensy" if not ps else "hay %d Teensys" % len(ps))
        return False
    try:
        t = c.Teensy(ps[0])
    except OSError as e:
        log("sin puerto:", e)
        return False
    try:
        st, ln = t.status()
        if st.get("off") != ("1" if off else "0"):
            t.cmd("o", 0 if off else 1)
            log("puesta o%d:" % (0 if off else 1), t.status()[1])
        return True
    except (RuntimeError, SystemExit) as e:  # status() hace sys.exit si no contesta
        log("no contestó:", e)
        return False
    finally:
        os.close(t.fd)


if len(c.puertos()) > 1:
    c.puerto()  # sale con el error: con dos Teensys no sabe cuál apagar

hh, mm = map(int, sys.argv[1].split(":"))
ahora = datetime.datetime.now()
hasta = ahora.replace(hour=hh, minute=mm, second=0, microsecond=0)
if hasta <= ahora:
    hasta += datetime.timedelta(days=1)
log("apagada hasta", hasta)

while (falta := (hasta - datetime.datetime.now()).total_seconds()) > 0:
    poner(True)
    time.sleep(min(CADA, falta))

for _ in range(20):  # el USB se cae de vez en cuando: reintenta ~10 min
    if poner(False):
        log("prendida")
        break
    time.sleep(30)
