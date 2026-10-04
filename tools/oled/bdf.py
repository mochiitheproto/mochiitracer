"""Minimal BDF 2.1 reader/writer (stdlib only) for the CJK pixel font of the OLED.

read(path)          -> (header_lines, {codepoint: Glyph}, ascent, descent)
Glyph.cell(asc, w)  -> the glyph as rows of 0/1 in a fixed cell (em rows from the top)
write(path, header_lines, glyphs, comments) -> a BDF with only those glyphs (same header)
"""


class Glyph:
    def __init__(self, name, code, lines, dwidth, bbx, rows):
        self.name, self.code, self.lines = name, code, lines   # lines = the raw STARTCHAR..ENDCHAR block
        self.dwidth = dwidth                                    # advance (px)
        self.bbx = bbx                                          # (w, h, xoff, yoff), yoff from the baseline
        self.rows = rows                                        # h ints, MSB = leftmost pixel

    def pixels(self):
        """[(x, y_from_baseline_up)] of the lit pixels."""
        w, h, xo, yo = self.bbx
        nbytes = (w + 7) // 8
        out = []
        for r, v in enumerate(self.rows):
            for x in range(w):
                if v >> (nbytes * 8 - 1 - x) & 1:
                    out.append((xo + x, yo + h - 1 - r))
        return out

    def cell(self, top, height, width):
        """Rows (lists of 0/1) of a width x height cell whose first row is `top` px above the
        baseline. Raises ValueError if ink falls outside the cell."""
        a = [[0] * width for _ in range(height)]
        for x, y in self.pixels():
            r = top - y
            if not (0 <= x < width and 0 <= r < height):
                raise ValueError(f"U+{self.code:04X}: pixel ({x},{y}) outside the {width}x{height} cell")
            a[r][x] = 1
        return a


def read(path):
    header, glyphs = [], {}
    ascent = descent = None
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    i = 0
    while i < len(lines) and not lines[i].startswith("STARTCHAR"):
        ln = lines[i]
        if not ln.startswith("CHARS ") and ln != "ENDFONT":
            header.append(ln)
        if ln.startswith("FONT_ASCENT "):
            ascent = int(ln.split()[1])
        elif ln.startswith("FONT_DESCENT "):
            descent = int(ln.split()[1])
        i += 1
    while i < len(lines):
        if not lines[i].startswith("STARTCHAR"):
            i += 1
            continue
        start = i
        name = lines[i].split(None, 1)[1] if " " in lines[i] else ""
        code, dwidth, bbx, rows, in_bitmap = None, None, None, [], False
        while not lines[i].startswith("ENDCHAR"):
            ln = lines[i]
            if in_bitmap:
                rows.append(int(ln, 16))
            elif ln.startswith("ENCODING "):
                code = int(ln.split()[1])
            elif ln.startswith("DWIDTH "):
                dwidth = int(ln.split()[1])
            elif ln.startswith("BBX "):
                bbx = tuple(int(v) for v in ln.split()[1:5])
            elif ln == "BITMAP":
                in_bitmap = True
            i += 1
        glyphs[code] = Glyph(name, code, lines[start:i + 1], dwidth, bbx, rows)
        i += 1
    if ascent is None or descent is None:
        raise ValueError(f"{path}: no FONT_ASCENT/FONT_DESCENT")
    return header, glyphs, ascent, descent


def write(path, header, glyphs, comments=()):
    out = []
    for ln in header:
        out.append(ln)
        if ln.startswith("STARTFONT"):
            out += [f"COMMENT {c}" for c in comments]
    out.append(f"CHARS {len(glyphs)}")
    for g in sorted(glyphs, key=lambda g: g.code):
        out += g.lines
    out.append("ENDFONT")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
