#!/usr/bin/env python3
"""boop_presets.json -> lib/ProtoTracer/Examples/Protogen/BoopPresets.h

The boop test patterns live in ONE table (boop_presets.json): the simulator reads the JSON and
the firmware's serial g<N> (fake finger, BoopScript.h) reads this generated header, so the head
and the sim replay exactly the same timeline. Edit the JSON, then run this.

  ./gen_boop_presets.py            rewrite the header
  ./gen_boop_presets.py --check    exit 1 if the header is not what the JSON gives (tests use it)
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
JSON = os.path.join(HERE, "boop_presets.json")
HEADER = os.path.join(HERE, "..", "..", "lib", "ProtoTracer", "Examples", "Protogen", "BoopPresets.h")


def render():
    with open(JSON) as f:
        presets = json.load(f)["presets"]
    rows = sorted(((int(p["g"]), name, p) for name, p in presets.items()), key=lambda r: r[0])
    if [g for g, _, _ in rows] != list(range(1, len(rows) + 1)):
        raise SystemExit("boop_presets.json: the g numbers must be 1..N without gaps")
    maxseg = max(len(p["pattern"]) for _, _, p in rows)
    for g, name, p in rows:
        if not all(0 < int(d) <= 65535 for d in p["pattern"]):
            raise SystemExit(f"boop_presets.json: {name}: durations must be 1..65535 ms")
    out = [
        "// GENERADO por tools/oledsim/gen_boop_presets.py desde tools/oledsim/boop_presets.json:",
        "// no se edita a mano (edita el JSON y corre el script). La misma tabla que usa el simulador.",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "namespace BoopPresets {",
        f"static constexpr uint8_t COUNT = {len(rows)};",
        f"static constexpr uint8_t MAX_SEGMENTS = {maxseg};",
        "",
        "// ms alternando PRENDIDO/apagado, empezando prendido (prendido = IsBooped() true).",
        "struct Preset {",
        "    uint8_t count;",
        "    uint16_t ms[MAX_SEGMENTS];",
        "};",
        "",
        "// índice = N - 1 del comando g<N>",
        "static constexpr Preset presets[COUNT] = {",
    ]
    for g, name, p in rows:
        ms = ", ".join(str(int(d)) for d in p["pattern"])
        out.append(f"    {{{len(p['pattern'])}, {{{ms}}}}},  // g{g} {name}: {p.get('desc', '')}")
    out += [
        "};",
        "",
        "// Sólo pa las pruebas en host (el firmware no las usa, el linker las tira).",
        "static constexpr const char* names[COUNT] = {" + ", ".join(f'"{n}"' for _, n, _ in rows) + "};",
        "// Eventos esperados (BoopGesture::Name), separados por espacio.",
        "static constexpr const char* expect[COUNT] = {",
    ]
    for g, name, p in rows:
        out.append(f'    "{" ".join(p["expect"])}",  // g{g} {name}')
    out += ["};", "}  // namespace BoopPresets", ""]
    return "\n".join(out)


def main():
    text = render()
    path = os.path.normpath(HEADER)
    if "--check" in sys.argv[1:]:
        try:
            cur = open(path).read()
        except OSError:
            cur = None
        if cur != text:
            print(f"{path} no corresponde a boop_presets.json: corre tools/oledsim/gen_boop_presets.py", file=sys.stderr)
            return 1
        print(f"ok: {os.path.relpath(path, os.path.join(HERE, '..', '..'))} == boop_presets.json")
        return 0
    with open(path, "w") as f:
        f.write(text)
    print(f"escrito {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
