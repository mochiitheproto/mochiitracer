#!/usr/bin/env bash
# Prueba en host del dedo de mentira (serial g<N>): la tabla generada está al día con
# boop_presets.json y cada preset da, con el BoopGesture.h real, los eventos esperados
# (12 ms por frame y 12 ms con un frame de 39 ms cada 5). Sale 0 si todo cuadra.
#   tools/oledsim/tests/boop_inject.sh
set -euo pipefail
SIM="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(cd "$SIM/../.." && pwd)"
python3 "$SIM/gen_boop_presets.py" --check
BIN="$(mktemp -t boop_inject.XXXXXX)"
trap 'rm -f "$BIN"' EXIT
"${CXX:-g++}" -std=gnu++17 -O1 -Wall -Wextra -Werror -I "$REPO/lib/ProtoTracer/Examples/Protogen" \
  "$SIM/tests/boop_inject_test.cpp" -o "$BIN"
"$BIN"
