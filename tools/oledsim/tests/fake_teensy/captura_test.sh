#!/usr/bin/env bash
# Prueba de punta a punta de tools/captura.py --gesto contra la cabeza de mentira (fake_teensy en
# un pty, con el HUD real y el parser de comandos/DumpOled/bloque del gesto copiados del firmware):
#   1. g1 (doble): cada frame capturado = uno del simulador (b01), sale el 1er boop y la palomita,
#      eventos tap1/hold/FIRE y la cara regresa a la que tenía (f<N>)
#   2. g3 (doble_tap): frames = simulador (b03), cancel:solto, la cara no cambia
#   3. sensor de boop apagado: ERR g1, captura.py sale con error y no toca nada
#   4. l<N> (check_idioma.py): OK/ERR, lang= en el estado, la OLED cambia de idioma al momento y
#      l255 la regresa a la del build; con la EEPROM falsa en un archivo, lo guardado en la 201
#      sobrevive un reinicio y l255 lo olvida (0xFF). Otra vez con un build -D IDIOMA_DEFECTO=1.
#   tools/oledsim/tests/fake_teensy/captura_test.sh [out_dir]
set -euo pipefail
H="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM="$(cd "$H/../.." && pwd)"
REPO="${PROTOTRACER_REPO:-$(cd "$SIM/../.." && pwd)}"
OUT="${1:-$(mktemp -d -t captura_test.XXXXXX)}"
mkdir -p "$OUT"
DISP="$REPO/lib/ProtoTracer/ExternalDevices/Displays"
BIN="$("$H/build.sh" "$DISP")"
"$SIM/render.sh" "$DISP" "$SIM/scenarios/boop.json" "$OUT/sim" --only b0 --no-sheet > "$OUT/sim.log"

PID=
cleanup() { if [ -n "$PID" ]; then kill "$PID" 2>/dev/null || true; fi; PID=; }   # con set -e: nunca truena
trap cleanup EXIT
start_fake() {   # args para fake_teensy ($FAKE, default $BIN); deja PTY y PID listos, ya pasado el arranque (~4 s)
    cleanup
    "${FAKE:-$BIN}" "$SIM" --secs 60 "$@" > "$OUT/pty.txt" &
    PID=$!
    for _ in $(seq 50); do [ -s "$OUT/pty.txt" ] && break; sleep 0.1; done
    PTY="$(head -1 "$OUT/pty.txt")"
    sleep 4.6
}
fail=0

start_fake --face 0
python3 "$REPO/tools/captura.py" --gesto 1 --puerto "$PTY" --out "$OUT/g1" | tee "$OUT/g1.log"
python3 "$H/check_captura.py" "$OUT/g1" 1 "$OUT/sim" b01_doble || fail=1
grep -q "eventos: tap1 +[0-9]*, hold +[0-9]*, FIRE +" "$OUT/g1.log" || { echo "g1: eventos raros"; fail=1; }
grep -q "^restaurado: S face=0 " "$OUT/g1.log" || { echo "g1: no regresó la cara 0"; fail=1; }
[ -s "$OUT/g1/gesto1_tira.png" ] || { echo "g1: sin tira"; fail=1; }

sleep 1.5   # que se acaben las chispitas del f0 con que captura.py regresó la cara
python3 "$REPO/tools/captura.py" --gesto 3 --puerto "$PTY" --out "$OUT/g3" | tee "$OUT/g3.log"
python3 "$H/check_captura.py" "$OUT/g3" 3 "$OUT/sim" b03_doble_tap || fail=1
grep -q "eventos: tap1 +[0-9]*, hold +[0-9]*, cancel:solto +" "$OUT/g3.log" || { echo "g3: eventos raros"; fail=1; }
grep -q "^restaurado: S face=0 " "$OUT/g3.log" || { echo "g3: cambió la cara"; fail=1; }

start_fake --face 0 --boop-off
if python3 "$REPO/tools/captura.py" --gesto 1 --puerto "$PTY" --out "$OUT/off" > "$OUT/off.log" 2>&1; then
    echo "boop apagado: captura.py debió fallar"; fail=1
fi
cat "$OUT/off.log"
grep -q "ERR g1" "$OUT/off.log" || { echo "boop apagado: no salió ERR g1"; fail=1; }
grep -q "^restaurado: S face=0 " "$OUT/off.log" || { echo "boop apagado: no restauró"; fail=1; }

EE="$OUT/eeprom.bin"
rm -f "$EE"
start_fake --face 0 --eeprom "$EE"
python3 "$H/check_idioma.py" "$PTY" --eeprom "$EE" | tee "$OUT/idioma.log" || fail=1
python3 "$H/check_idioma.py" "$PTY" --eeprom "$EE" --espera 0 --manda l2 | tee -a "$OUT/idioma.log" || fail=1
start_fake --face 0 --eeprom "$EE"   # reinicio: arranca en lo que quedó en la EEPROM
python3 "$H/check_idioma.py" "$PTY" --eeprom "$EE" --espera 2 --manda l255 | tee -a "$OUT/idioma.log" || fail=1
start_fake --face 0 --eeprom "$EE"   # reinicio: EEPROM 201 = 0xFF otra vez -> el del build
python3 "$H/check_idioma.py" "$PTY" --eeprom "$EE" --espera 0 | tee -a "$OUT/idioma.log" || fail=1

cleanup   # build en inglés (ANGRY: ENOJO/ANGRY, pa que la OLED también lo distinga)
FAKE="$OUT/fake_teensy_idioma1"
cp "$(OLEDSIM_DEFS="${OLEDSIM_DEFS:-} -DIDIOMA_DEFECTO=1" "$H/build.sh" "$DISP")" "$FAKE"
start_fake --face 1
python3 "$H/check_idioma.py" "$PTY" | tee -a "$OUT/idioma.log" || fail=1
grep -q "build lang=1" "$OUT/idioma.log" || { echo "IDIOMA_DEFECTO=1: no arrancó en inglés"; fail=1; }

[ $fail = 0 ] && echo "captura --gesto: todo bien ($OUT)" || echo "captura --gesto: HAY FALLAS ($OUT)"
exit $fail
