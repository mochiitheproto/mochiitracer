#!/usr/bin/env bash
# Compila el motor real de ProtoTracer (desde el repo, sin tocarlo) + el driver host.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
L="${PROTOTRACER_LIB:-$(cd "$HERE/../../lib/ProtoTracer" && pwd)}"   # el lib/ProtoTracer de este repo
CXX="${CXX:-g++}"
FLAGS="-std=gnu++17 -O2 -w -I$HERE/shim -I$L"
mkdir -p "$HERE/build" "$HERE/bin"
SRCS=$(ls $L/Utils/Math/*.cpp $L/Utils/*.cpp $L/Utils/Filter/*.cpp $L/Utils/Signals/*.cpp \
  $L/Utils/Time/*.cpp $L/Renderer/Utils/*.cpp $L/Renderer/Rasterizer/*.cpp $L/Renderer/DisplayTest/*.cpp \
  $L/Scene/*.cpp $L/Scene/Objects/*.cpp $L/Scene/Materials/Static/*.cpp $L/Scene/Materials/Animated/*.cpp \
  $L/Scene/Screenspace/Effect.cpp $L/Scene/Screenspace/Passthrough.cpp $L/Camera/*.cpp \
  $L/Camera/CameraManager/*.cpp $L/Camera/Pixels/*.cpp $L/Animation/*.cpp $L/Physics/Utils/*.cpp \
  $L/Assets/Models/FBX/Utils/*.cpp $L/Engine/*.cpp 2>/dev/null)
OBJS=()
for f in $SRCS; do
  o="$HERE/build/$(echo "${f#$L/}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then $CXX $FLAGS -c "$f" -o "$o"; fi
  OBJS+=("$o")
done
$CXX $FLAGS "$HERE/src/hostrender.cpp" "${OBJS[@]}" -o "$HERE/bin/hostrender"
echo "ok: $HERE/bin/hostrender"
