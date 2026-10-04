#!/usr/bin/env bash
# build.sh [displays_dir]  ->  imprime la ruta del binario fake_teensy.
# Usa el build del simulador (objetos del diseño + libcommon + el stage del repo) y copia TAL
# CUAL de lib/ProtoTracer/Examples/Protogen/ProtogenHUB75Project.h: ReadSerialCommands(),
# RunCommand(), DumpOled(), la lectura de la EEPROM de Update() (if (!calLoaded) ...) y el bloque
# del gesto de Update() (if bootActive ... else ...).
set -euo pipefail
H="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM="$(cd "$H/../.." && pwd)"
REPO="${PROTOTRACER_REPO:-$(cd "$SIM/../.." && pwd)}"
DEPS="${OLEDSIM_LIBDEPS:-$REPO/.pio/libdeps/teensy40hub75}"
DISP="${1:-$REPO/lib/ProtoTracer/ExternalDevices/Displays}"

SIMBIN="$(PROTOTRACER_REPO="$REPO" OLEDSIM_LIBDEPS="$DEPS" "$SIM/build.sh" "$DISP")"
W="$(dirname "$SIMBIN")"

CMDBUF="$(python3 - "$REPO/lib/ProtoTracer/Examples/Protogen/ProtogenHUB75Project.h" "$W/fake_extract.inc" <<'PY'
import re, sys
src = open(sys.argv[1]).read()

def block(start):
    """from src[start] through the brace that closes the first '{' after it"""
    depth, j = 0, src.index('{', start)
    for k in range(j, len(src)):
        if src[k] == '{':
            depth += 1
        elif src[k] == '}':
            depth -= 1
            if depth == 0:
                return k + 1
    raise SystemExit('unbalanced braces')

out = []
for name in ('ReadSerialCommands', 'RunCommand', 'DumpOled'):
    i = src.index(f'    void {name}() {{')
    out.append(src[i:block(i)])
i = src.index('        if (!calLoaded) {')
out.append('    void LoadEeprom() {\n' + src[i:block(i)] + '\n    }')
i = src.index('        if (bootActive || !Menu::UseBoopSensor()) {')
e = block(i)
m = re.compile(r'\s*else\s*').match(src, e)
if not m:
    raise SystemExit('gesture block: no else')
e = block(m.end())
out.append('    void GestureBlock(bool bootActive, uint32_t boopNow) {\n' + src[i:e] + '\n    }')
open(sys.argv[2], 'w').write('// COPIADO de ProtogenHUB75Project.h por build.sh: no editar\n' + '\n\n'.join(out) + '\n')
print(re.search(r'char\s+cmdBuf\[(\d+)\]', src).group(1))
PY
)"

DEFS="-DARDUINO=10819 -DTEENSYDUINO=159 -DARDUINO_TEENSY40 -D__IMXRT1062__ -DF_CPU=600000000 -DPROJECT_PROTOGEN_HUB75 -DLAYOUT_US_ENGLISH"
STAGE="$W/stage/lib/ProtoTracer"
"${CXX:-g++}" -std=gnu++17 -O1 -fpermissive -fno-exceptions -fno-rtti -Wno-psabi -w $DEFS ${OLEDSIM_DEFS:-} -DFAKE_CMDBUF="$CMDBUF" \
  -I"$SIM/shim" -I"$DEPS/Adafruit GFX Library" -I"$DEPS/Adafruit SSD1306" -I"$STAGE" -I"$STAGE/ExternalDevices/Displays" -I"$W" \
  -c "$H/fake_teensy.cpp" -o "$W/fake_teensy.o"
shopt -s nullglob
"${CXX:-g++}" -no-pie "$W/fake_teensy.o" "$W"/obj/design__*.o "$W"/obj/design_c__*.o "$SIM/build/common/libcommon.a" -o "$W/fake_teensy"
echo "$W/fake_teensy"
