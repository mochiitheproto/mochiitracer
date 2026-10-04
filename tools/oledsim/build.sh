#!/usr/bin/env bash
# build.sh <displays_dir>  ->  prints the path of the simulator binary for that design.
#
# 1. common objects (once, cached in build/common, rebuilt when a source is newer):
#    Teensy Print/WString ports + sim runtime (clock, Wire->SSD1306 emulator, tempmon),
#    the REAL Adafruit_GFX and Adafruit_SSD1306 from .pio/libdeps, and the bits of the
#    ProtoTracer engine the HUD links against (Math, RGBColor, TimeStep, Effect...).
# 2. per design (build/designs/<hash of dir>/): a stage tree = repo lib/ProtoTracer as
#    symlinks, with ExternalDevices/Displays replaced by a COPY of <displays_dir> and
#    Menu.h replaced by the simulator's Menu stand-in, so the design's relative includes
#    ("../InputDevices/Menu/Menu.h", "../../Utils/...") resolve exactly as in the repo.
#    Every *.cpp/*.c in <displays_dir> is compiled, plus the driver, then linked.
# The repo itself is only read, never written.
# OLEDSIM_DEFS="-DESPECIE_PRIMAGEN -DIDIOMA_DEFECTO=1" adds build flags to the design and the driver
# (like PlatformIO's build_flags; the common objects do not depend on them).
set -euo pipefail
SIM="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${PROTOTRACER_REPO:-$(cd "$SIM/../.." && pwd)}"
LIB="$REPO/lib/ProtoTracer"
DEPS="${OLEDSIM_LIBDEPS:-$REPO/.pio/libdeps/teensy40hub75}"
GFX="$DEPS/Adafruit GFX Library"
SSD="$DEPS/Adafruit SSD1306"
CXX="${CXX:-g++}"
CC="${CC:-gcc}"

[ $# -ge 1 ] || { echo "usage: $0 <displays_dir>" >&2; exit 2; }
DESIGN="$(cd "$1" && pwd)"
[ -f "$DESIGN/SSD1306.h" ] || { echo "build.sh: $DESIGN has no SSD1306.h" >&2; exit 2; }

DEFS="-DARDUINO=10819 -DTEENSYDUINO=159 -DARDUINO_TEENSY40 -D__IMXRT1062__ -DF_CPU=600000000 -DPROJECT_PROTOGEN_HUB75 -DLAYOUT_US_ENGLISH"
WARN="${OLEDSIM_WARN:+-Wall}"
WARN="${WARN:--w}"
BASEFLAGS="-std=gnu++17 -O1 -fpermissive -fno-exceptions -fno-rtti -Wno-psabi $DEFS"
CFLAGS_C="-O1 $DEFS -w"

# ---------------------------------------------------------------- common
COMMON="$SIM/build/common"
mkdir -p "$COMMON"
(
  flock 9
  INC=(-I"$SIM/shim" -I"$GFX" -I"$SSD" -I"$LIB")
  SRCS=(
    "$SIM/shim/WString.cpp" "$SIM/shim/Print.cpp" "$SIM/shim/avr_functions.cpp" "$SIM/shim/oledsim_rt.cpp"
    "$GFX/Adafruit_GFX.cpp" "$SSD/Adafruit_SSD1306.cpp"
    "$LIB"/Utils/Math/*.cpp "$LIB/Utils/RGBColor.cpp" "$LIB"/Utils/Time/*.cpp
    "$LIB/Physics/Utils/BoundingBox2D.cpp" "$LIB/Physics/Utils/DampedSpring.cpp"
    "$LIB/Scene/Screenspace/Effect.cpp"
  )
  OBJS=()
  changed=0
  for f in "${SRCS[@]}"; do
    o="$COMMON/$(basename "$(dirname "$f")" | tr ' ' '_')__$(basename "${f%.*}").o"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ "$SIM/shim/Arduino.h" -nt "$o" ] || [ "$SIM/shim/oledsim_rt.h" -nt "$o" ] || [ "$SIM/shim/Wire.h" -nt "$o" ]; then
      $CXX $BASEFLAGS -w "${INC[@]}" -c "$f" -o "$o"
      changed=1
    fi
    OBJS+=("$o")
  done
  if [ $changed = 1 ] || [ ! -f "$COMMON/libcommon.a" ]; then
    rm -f "$COMMON/libcommon.a"
    ar rcs "$COMMON/libcommon.a" "${OBJS[@]}"
  fi
) 9>"$COMMON/.lock"

# ---------------------------------------------------------------- design
KEY="$(printf '%s' "$DESIGN" | sha1sum | cut -c1-12)"
W="$SIM/build/designs/$KEY"
STAGE="$W/stage"
mkdir -p "$W"
(
  flock 9
  rm -rf "$STAGE" "$W/obj"
  mkdir -p "$STAGE/lib" "$W/obj"
  cp -as "$LIB" "$STAGE/lib/ProtoTracer"
  rm -rf "$STAGE/lib/ProtoTracer/ExternalDevices/Displays"
  cp -rL "$DESIGN" "$STAGE/lib/ProtoTracer/ExternalDevices/Displays"
  rm -f "$STAGE/lib/ProtoTracer/ExternalDevices/InputDevices/Menu/Menu.h"
  cp "$SIM/overlay/ExternalDevices/InputDevices/Menu/Menu.h" "$STAGE/lib/ProtoTracer/ExternalDevices/InputDevices/Menu/Menu.h"
  printf '%s\n' "$DESIGN" > "$W/design_path.txt"

  D="$STAGE/lib/ProtoTracer/ExternalDevices/Displays"
  INC=(-I"$SIM/shim" -I"$GFX" -I"$SSD" -I"$STAGE/lib/ProtoTracer" -I"$D")
  OBJS=()
  shopt -s nullglob
  for f in "$D"/*.cpp; do
    o="$W/obj/design__$(basename "${f%.*}").o"
    $CXX $BASEFLAGS ${OLEDSIM_DEFS:-} $WARN "${INC[@]}" -c "$f" -o "$o"
    OBJS+=("$o")
  done
  for f in "$D"/*.c; do
    o="$W/obj/design_c__$(basename "${f%.*}").o"
    $CC $CFLAGS_C ${OLEDSIM_DEFS:-} "${INC[@]}" -c "$f" -o "$o"
    OBJS+=("$o")
  done
  $CXX $BASEFLAGS ${OLEDSIM_DEFS:-} -w "${INC[@]}" -c "$SIM/src/oledsim_main.cpp" -o "$W/obj/driver.o"
  $CXX -no-pie "$W/obj/driver.o" "${OBJS[@]}" "$COMMON/libcommon.a" -o "$W/oledsim"
) 9>"$W/.lock"

echo "$W/oledsim"
