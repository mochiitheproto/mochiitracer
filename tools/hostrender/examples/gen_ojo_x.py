#!/usr/bin/env python3
"""Genera morphs/ojo_x.json: el ojo de NukudeFlat convertido en una X (abanico desde el centro).

Topología usada (ver README, "malla del ojo"): con los vértices 2, 7 y 8 en el centro C,
los 16 triángulos del ojo quedan como 11 cuñas (C, a, b) + el triángulo (13,1,6) + 4
triángulos degenerados. El contorno en sentido horario es
    13 → 1 → 6 → 5 → 12 → 10 → 3 → 14 → 9 → 15 → 4 → 11
y se le asignan los 12 vértices del contorno de la X:
    13 muesca arriba | 1,6 punta arriba-der | 5 muesca der | 12,10 punta abajo-der
    3 muesca abajo   | 14,9 punta abajo-izq | 15 muesca izq | 4,11 punta arriba-izq
Uso: gen_ojo_x.py [cx cy hx hy grosor] > morphs/ojo_x.json   (coords de pixel, panel normal)

OJO bbox: la alineación (Stretch) siempre manda el vértice más a la derecha a la col 60.13
(facesize 9). En la cara normal ese vértice es el 5 (punta del ojo); en la X el 5 es una
muesca, así que la punta derecha de la X se ajusta sola (hx) pa quedar justo en 60.13 y
que la boca/nariz no se estiren. Pon RIGHT_EDGE=None pa desactivarlo.
"""
RIGHT_EDGE = 60.13
import json, math, sys

cx, cy, hx, hy, t = (float(v) for v in (sys.argv[1:6] if len(sys.argv) >= 6 else (50.5, 6.6, 9.6, 5.0, 2.6)))
h = t / 2.0


def unit(x, y):
    n = math.hypot(x, y)
    return x / n, y / n


def isect(p, d, q, e):
    # p + s d = q + u e
    det = d[0] * (-e[1]) - d[1] * (-e[0])
    s = ((q[0] - p[0]) * (-e[1]) - (q[1] - p[1]) * (-e[0])) / det
    return (p[0] + s * d[0], p[1] + s * d[1])


d1 = unit(hx, hy)            # barra 1: arriba-izq → abajo-der (renglón crece hacia abajo)
d2 = unit(hx, -hy)           # barra 2: abajo-izq → arriba-der
n1 = (d1[1], -d1[0])         # normal de barra 1 que apunta "arriba-der"
n2 = (-d2[1], d2[0])         # normal de barra 2 que apunta "abajo-der"
C = (cx, cy)
off = lambda P, n, k: (P[0] + n[0] * k, P[1] + n[1] * k)
UR, DR, DL, UL = (cx + hx, cy - hy), (cx + hx, cy + hy), (cx - hx, cy + hy), (cx - hx, cy - hy)
b1_up, b1_dn = off(C, n1, h), off(C, n1, -h)       # puntos en los bordes de barra 1
b2_dn, b2_up = off(C, n2, h), off(C, n2, -h)       # bordes de barra 2
notch_top = isect(b1_up, d1, b2_up, d2)
notch_right = isect(b1_up, d1, b2_dn, d2)
notch_bot = isect(b1_dn, d1, b2_dn, d2)
notch_left = isect(b1_dn, d1, b2_up, d2)
pos = {
    13: notch_top, 1: off(UR, n2, -h), 6: off(UR, n2, h), 5: notch_right,
    12: off(DR, n1, h), 10: off(DR, n1, -h), 3: notch_bot,
    14: off(DL, n2, h), 9: off(DL, n2, -h), 15: notch_left,
    4: off(UL, n1, -h), 11: off(UL, n1, h),
    2: C, 7: C, 8: C,
}
if RIGHT_EDGE is not None:
    for _ in range(20):
        mx = max(p[0] for p in pos.values())
        if abs(mx - RIGHT_EDGE) < 1e-4:
            break
        cx += (RIGHT_EDGE - mx) * 0.0      # el centro se queda; se ajusta el semieje
        hx += (RIGHT_EDGE - mx)
        d1 = unit(hx, hy); d2 = unit(hx, -hy)
        n1 = (d1[1], -d1[0]); n2 = (-d2[1], d2[0])
        UR, DR, DL, UL = (cx + hx, cy - hy), (cx + hx, cy + hy), (cx - hx, cy + hy), (cx - hx, cy - hy)
        b1_up, b1_dn = off(C, n1, h), off(C, n1, -h)
        b2_dn, b2_up = off(C, n2, h), off(C, n2, -h)
        pos = {
            13: isect(b1_up, d1, b2_up, d2), 1: off(UR, n2, -h), 6: off(UR, n2, h), 5: isect(b1_up, d1, b2_dn, d2),
            12: off(DR, n1, h), 10: off(DR, n1, -h), 3: isect(b1_dn, d1, b2_dn, d2),
            14: off(DL, n2, h), 9: off(DL, n2, -h), 15: isect(b1_dn, d1, b2_up, d2),
            4: off(UL, n1, -h), 11: off(UL, n1, h), 2: C, 7: C, 8: C,
        }

print(json.dumps({
    "name": "OjoX",
    "frames": 15,
    "interp": "Overshoot",
    "pin": "all",
    "comment": f"X centrada en ({cx},{cy}) px, semiejes {hx}x{hy}, grosor {t}. Generado por examples/gen_ojo_x.py",
    "pixel_positions": {str(k): [round(v[0], 3), round(v[1], 3)] for k, v in sorted(pos.items())},
}, indent=1))
