#!/usr/bin/env bash
# render.sh <displays_dir> [scenario.json] <out_dir> [--scale N] [--tint white|blue|yb] [--only SUBSTR]
# Rebuilds the simulator for the design and renders every state (default: scenarios/standard.json).
exec python3 "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/render.py" "$@"
