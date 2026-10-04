#!/usr/bin/env bash
# Graba el osciloscopio de audio (líneas "A ..." del comando 'a1': magnitud,
# temperatura y espectro de 128 bins por frame) N segundos, aguantando caídas
# del USB igual que grabar-boop.sh. Pa calibrar la boca (visemes) en la laptop.
#   ./grabar-audio.sh [segundos=120] [etiqueta]   → ~/audio-sesion-<fecha>[-etiqueta].log
DUR=${1:-120}
# Puerto serial del Teensy: el symlink estable de /dev/serial/by-id. TEENSY_PORT=<ruta> lo fija a
# mano. Regresa 1 si no hay ninguno y 2 (con el error en stderr) si hay más de uno.
teensy_port() {
    if [ -n "${TEENSY_PORT:-}" ]; then
        [ -e "$TEENSY_PORT" ] || return 1
        echo "$TEENSY_PORT"; return 0
    fi
    local ports=() p
    for p in /dev/serial/by-id/usb-Teensyduino_USB_Serial_*; do
        [ -e "$p" ] && ports+=("$p")
    done
    case ${#ports[@]} in
        0) return 1 ;;
        1) echo "${ports[0]}" ;;
        *) echo "✗ hay ${#ports[@]} Teensys conectados: ${ports[*]} (elige uno con TEENSY_PORT=<ruta>)" >&2
           return 2 ;;
    esac
}
teensy_port >/dev/null; [ $? -ne 2 ] || exit 1
L=~/audio-sesion-$(date +%F-%H%M%S)${2:+-$2}.log
ln -sf "$L" ~/audio-sesion-actual.log
END=$(( $(date +%s) + DUR ))
echo "# inicio $(date +%T) dur=$DUR ${2:-}" >> "$L"

while [ "$(date +%s)" -lt "$END" ]; do
    if T=$(teensy_port 2>/dev/null); then
        rem=$(( END - $(date +%s) ))
        [ "$rem" -gt 0 ] || break   # ojo: "timeout 0" = sin límite
        stty -F "$T" raw -echo 2>/dev/null
        echo "# conectado $(date +%T)" >> "$L"
        ( sleep 0.3; printf "a1\n" > "$T"; sleep 0.2; printf "s\n" > "$T" ) 2>/dev/null &
        t0=$(date +%s)
        timeout "$rem" cat "$T" | grep -a --line-buffered -E "^(A |S |\[boop\]|\[boot\])" >> "$L"
        echo "# desconectado $(date +%T)" >> "$L"
        [ $(( $(date +%s) - t0 )) -ge 1 ] || sleep 1
    fi
    sleep 0.2
done
T=$(teensy_port 2>/dev/null) && printf "a0\n" > "$T" 2>/dev/null
echo "# fin $(date +%T)" >> "$L"
echo "$L: $(grep -c '^A ' "$L") frames"
