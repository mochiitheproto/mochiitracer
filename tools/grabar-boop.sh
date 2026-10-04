#!/usr/bin/env bash
# Graba el osciloscopio del boop (líneas "P ..." + eventos [boop]/[boot]) por N
# segundos, aguantando caídas del USB: cada vez que el Teensy regresa reabre el
# puerto y vuelve a mandar 'p'. Si tras una caída sale "[boot]", fue reinicio.
#   ./grabar-boop.sh [segundos=900]     → ~/boop-sesion-<fecha>.log (+ symlink boop-sesion-actual.log)
DUR=${1:-900}
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
L=~/boop-sesion-$(date +%F-%H%M).log
ln -sf "$L" ~/boop-sesion-actual.log
END=$(( $(date +%s) + DUR ))

while [ "$(date +%s)" -lt "$END" ]; do
    if T=$(teensy_port 2>/dev/null); then
        rem=$(( END - $(date +%s) ))
        [ "$rem" -gt 0 ] || break   # ojo: "timeout 0" = sin límite
        stty -F "$T" raw -echo 2>/dev/null
        echo "# conectado $(date +%T)" >> "$L"
        ( sleep 0.3; printf "p\n" > "$T" ) 2>/dev/null &
        t0=$(date +%s)
        timeout "$rem" cat "$T" >> "$L" 2>/dev/null
        echo "# desconectado $(date +%T)" >> "$L"
        [ $(( $(date +%s) - t0 )) -ge 1 ] || sleep 1   # si ni abrió, no spamear
    fi
    sleep 0.2
done
T=$(teensy_port 2>/dev/null) && printf "q\n" > "$T" 2>/dev/null
echo "# fin $(date +%T)" >> "$L"
