#!/usr/bin/env python3
"""
Genera TestGrid.h — imagen estática 64x32 con grid de calibración.

Marcas:
- Border outer (amarillo) en X=0,63 e Y=0,31
- Esquinas 3x3:
    TL (0,0)   = ROJO       (origen)
    TR (61,0)  = VERDE
    BL (0,29)  = AZUL
    BR (61,29) = BLANCO
- Líneas verticales cada 4 columnas (CYAN tenue)
- Líneas horizontales cada 4 filas (MAGENTA tenue)
- Centro (31,15) cross blanco
- Letra "L" pixel-art en zona superior izq (asymmetry pa detectar mirror X)
"""
from PIL import Image, ImageDraw

W, H = 64, 32
BG    = (0, 0, 0)
RED   = (255, 0, 0)
GREEN = (0, 255, 0)
BLUE  = (0, 90, 255)
WHITE = (240, 240, 240)
YEL   = (255, 230, 0)
CYAN  = (0, 255, 255)   # full saturado para que se vea
MAGN  = (255, 0, 255)   # full saturado para que se vea


def build():
    img = Image.new("RGB", (W, H), BG)

    # Gridlines internas — cyan vertical cada 8 cols, magenta horizontal cada 4 rows (más densas)
    for x in range(8, W, 8):
        for y in range(0, H):
            img.putpixel((x,     y), CYAN)
            img.putpixel((x + 1, y), CYAN)
    for y in range(4, H, 4):
        for x in range(0, W):
            img.putpixel((x, y),     MAGN)
            img.putpixel((x, y + 1), MAGN)

    # Border 3px insetado (en X=2..4 e Y=2..4 desde los edges) — para evitar el crop del lado izq
    INSET = 2
    THICK = 3
    for thick in range(THICK):
        for x in range(INSET, W - INSET):
            img.putpixel((x, INSET + thick),             YEL)
            img.putpixel((x, H - 1 - INSET - thick),     YEL)
        for y in range(INSET, H - INSET):
            img.putpixel((INSET + thick,             y), YEL)
            img.putpixel((W - 1 - INSET - thick,     y), YEL)

    # Corner squares 6x6, también insetadas para evitar crop
    def square(x0, y0, color, sz=6):
        for dx in range(sz):
            for dy in range(sz):
                if 0 <= x0 + dx < W and 0 <= y0 + dy < H:
                    img.putpixel((x0 + dx, y0 + dy), color)

    square(INSET,         INSET,         RED)    # TL origen (insetada)
    square(W - INSET - 6, INSET,         GREEN)  # TR
    square(INSET,         H - INSET - 6, BLUE)   # BL
    square(W - INSET - 6, H - INSET - 6, WHITE)  # BR

    # Centro: cross blanco (centro lógico = 31.5, 15.5 → uso 31,15)
    cx, cy = 31, 15
    for dx in range(-3, 4):
        if 0 <= cx + dx < W:
            img.putpixel((cx + dx, cy), WHITE)
    for dy in range(-3, 4):
        if 0 <= cy + dy < H:
            img.putpixel((cx, cy + dy), WHITE)

    # Letra "L" 5x7 en top-left interior (X=8..12, Y=8..14) — asymmetry
    L_bitmap = [
        "X....",
        "X....",
        "X....",
        "X....",
        "X....",
        "X....",
        "XXXXX",
    ]
    for ry, row in enumerate(L_bitmap):
        for rx, ch in enumerate(row):
            if ch == "X":
                px = 8 + rx
                py = 8 + ry
                if 0 <= px < W and 0 <= py < H:
                    img.putpixel((px, py), WHITE)

    return img


def quantize(img, n_colors=8):
    pal_img = img.convert("P", palette=Image.ADAPTIVE, colors=n_colors)
    palette = pal_img.getpalette()[: n_colors * 3]
    while len(palette) < n_colors * 3:
        palette.extend([0, 0, 0])
    return pal_img, palette


def emit(out_path):
    img = build()
    img.resize((W * 4, H * 4), Image.NEAREST).save("/tmp/testgrid_preview.png")
    pal_img, palette = quantize(img, 8)
    n_colors = 8
    data = list(pal_img.getdata())

    L = []
    L.append("#pragma once\n\n")
    L.append('#include "../../../Scene/Materials/Static/Image.h"\n\n')
    L.append("class TestGrid : public Image {\n")
    L.append("private:\n")
    L.append("    static const uint8_t rgbMemory[];\n")
    L.append("    static const uint8_t rgbColors[];\n\n")
    L.append("public:\n")
    L.append(f"    TestGrid(Vector2D size, Vector2D offset) : Image(rgbMemory, rgbColors, {W}, {H}, {n_colors - 1}) {{\n")
    L.append("        SetSize(size);\n")
    L.append("        SetPosition(offset);\n")
    L.append("    }\n")
    L.append("};\n\n")
    L.append("const uint8_t TestGrid::rgbMemory[] PROGMEM = {")
    L.append(",".join(str(x) for x in data))
    L.append("};\n\n")
    L.append("const uint8_t TestGrid::rgbColors[] PROGMEM = {")
    L.append(",".join(f"{palette[i*3]},{palette[i*3+1]},{palette[i*3+2]}" for i in range(n_colors)))
    L.append("};\n")

    with open(out_path, "w") as f:
        f.write("".join(L))
    print(f"-> {out_path} ({W}x{H}, {n_colors} colors). Preview: /tmp/testgrid_preview.png")


if __name__ == "__main__":
    import sys
    emit(sys.argv[1])
