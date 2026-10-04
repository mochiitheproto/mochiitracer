#!/usr/bin/env bash
# Compila + respalda + flashea el Teensy de la cabeza, y verifica que regrese vivo.
#   ./flash.sh <descripcion>          build del working tree → backup → flash
#   ./flash.sh --hex <archivo.hex>    flashea un hex ya hecho (rescate: uno bueno de ~/protogen-backup/)
#   ./flash.sh --build-only           sólo compila y reporta md5 / líneas
set -euo pipefail

# El repo: el papá de tools/ (también corriendo por un symlink), o el ProtoTracer/ de al lado si
# esto es una copia suelta junto al repo. PROTOTRACER_REPO=<ruta> lo fija a mano.
HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
if [ -n "${PROTOTRACER_REPO:-}" ]; then REPO=$PROTOTRACER_REPO
elif [ -f "$HERE/../platformio.ini" ]; then REPO="$(cd "$HERE/.." && pwd)"
elif [ -f "$HERE/ProtoTracer/platformio.ini" ]; then REPO="$HERE/ProtoTracer"
else echo "✗ no encuentro el repo (platformio.ini) junto a $HERE; usa PROTOTRACER_REPO=<ruta>" >&2; exit 1
fi
BACKUP=${PROTOGEN_BACKUP:-~/protogen-backup}
ENV=teensy40hub75
LOADER=~/.platformio/packages/tool-teensy/teensy_loader_cli
PIO=$(command -v pio || echo ~/.local/bin/pio)
HEX_MAX_LINES=65536   # buffer interno de teensy_loader_cli (HEX parse error arriba de esto)

die() { echo "✗ $*" >&2; exit 1; }

# Puerto serial del Teensy: el symlink estable de /dev/serial/by-id (ttyACM* cambia de número si
# algo se quedó con el viejo abierto). TEENSY_PORT=<ruta> lo fija a mano. Regresa 1 si no hay
# ninguno y 2 (con el error en stderr) si hay más de uno.
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

build() {
    local log; log=$(mktemp)
    # ojo: si el build truena NO hay que seguir, el firmware.hex viejo sigue ahí
    if ! (cd "$REPO" && "$PIO" run -e "$ENV") >"$log" 2>&1; then
        grep -iE "error" "$log" | head -20; rm -f "$log"; die "build falló"
    fi
    grep -E "teensy_size: +(FLASH|RAM1)" "$log" || true
    rm -f "$log"
    HEX="$REPO/.pio/build/$ENV/firmware.hex"
    lines=$(wc -l < "$HEX")
    echo "hex: $lines líneas (margen $((HEX_MAX_LINES - lines))) md5 $(md5sum < "$HEX" | cut -c1-32)"
    [ "$lines" -lt "$HEX_MAX_LINES" ] || die "pasa el límite del loader, recorta frames antes"
}

flash() {
    local hex=$1 out="" serial="" rc=0
    # con dos Teensys el loader agarra cualquiera: mejor no flashear a ciegas
    teensy_port >/dev/null || rc=$?
    [ "$rc" -ne 2 ] || exit 1
    for i in 1 2 3; do
        out=$("$LOADER" --mcu=TEENSY40 -w -s -v "$hex" 2>&1 | tail -1) || true
        echo "intento $i: $out"
        [ "$out" = "Booting" ] && break
        sleep 1
    done
    [ "$out" = "Booting" ] || die "no flasheó (¿botón PROGRAM del Teensy?)"

    for _ in $(seq 1 30); do serial=$(teensy_port 2>/dev/null) && break; sleep 0.5; done
    [ -n "$serial" ] || die "flasheó pero el serial no regresó"
    sleep 2
    # sin eco: si no, todo lo que imprime el Teensy le regresa como comandos
    stty -F "$serial" raw -echo 2>/dev/null || true
    fps=$(timeout 3 cat "$serial" 2>/dev/null | grep -a -m1 "FPS:" || true)
    [ -n "$fps" ] && echo "✓ viva: $fps" || die "el serial regresó pero no escupe FPS — revisar"
}

case "${1:-}" in
    --hex)
        [ -f "${2:-}" ] || die "uso: $0 --hex <archivo.hex>"
        flash "$2" ;;
    --build-only)
        build ;;
    ""|-*)
        sed -n '2,5p' "$0"; exit 1 ;;
    *)
        desc=$1
        dest="$BACKUP/firmware_${desc}_$(date +%F).hex"
        [ -e "$dest" ] && die "ya existe $dest — usa otra descripción"
        build
        mkdir -p "$BACKUP"   # la primera vez en otra máquina todavía no existe
        cp "$HEX" "$dest" && echo "backup: $dest"
        flash "$HEX" ;;
esac
