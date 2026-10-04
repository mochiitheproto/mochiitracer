"""Bitmap helpers for the OLED assets: ASCII art -> arrays, the EPX 2x smoother, packing,
and exact Python ports of the Adafruit_GFX primitives (so widgets can be baked into bitmaps and
the GFX circle/round-rect code is never linked on the Teensy)."""
import numpy as np


def parse(rows):
    """ASCII art -> uint8 array ('#' = lit, anything else = off). Rows are padded to the widest."""
    w = max(len(r) for r in rows)
    return np.array([[1 if c == '#' else 0 for c in r.ljust(w, '.')] for r in rows], np.uint8)


def epx(a):
    """EPX / Scale2x: 1 px strokes -> 2 px strokes with rounded diagonals (the 'soft' look)."""
    h, w = a.shape
    p = np.pad(a, 1)
    out = np.zeros((h * 2, w * 2), np.uint8)
    for y in range(h):
        for x in range(w):
            P = p[y + 1, x + 1]
            A = p[y, x + 1]; B = p[y + 1, x + 2]; C = p[y + 1, x]; D = p[y + 2, x + 1]
            o1 = o2 = o3 = o4 = P
            if C == A and C != D and A != B: o1 = A
            if A == B and A != C and B != D: o2 = B
            if D == C and D != B and C != A: o3 = C
            if B == D and B != A and D != C: o4 = D
            out[2 * y, 2 * x] = o1; out[2 * y, 2 * x + 1] = o2
            out[2 * y + 1, 2 * x] = o3; out[2 * y + 1, 2 * x + 1] = o4
    return out


def pack(a):
    """Row-major, MSB first, rows padded to whole bytes: the Adafruit_GFX drawBitmap() format."""
    h, w = a.shape
    data = []
    for y in range(h):
        for b in range((w + 7) // 8):
            v = 0
            for k in range(8):
                x = b * 8 + k
                if x < w and a[y, x]:
                    v |= 0x80 >> k
            data.append(v)
    return data


def crop(a):
    ys, xs = np.nonzero(a)
    if len(xs) == 0:
        return a[:0, :0]
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def recenter(a, W, H):
    """Crop to the lit pixels and centre in a W x H box."""
    c = crop(a)
    h, w = c.shape
    assert w <= W and h <= H, (w, h, W, H)
    o = np.zeros((H, W), np.uint8)
    x0, y0 = (W - w) // 2, (H - h) // 2
    o[y0:y0 + h, x0:x0 + w] = c
    return o


class G:
    """Exact ports of the Adafruit_GFX primitives the HUD used to call at runtime."""

    def __init__(self, w, h):
        self.a = np.zeros((h, w), np.uint8)

    def px(self, x, y, c=1):
        if 0 <= x < self.a.shape[1] and 0 <= y < self.a.shape[0]:
            self.a[y, x] = c

    def hline(self, x, y, w, c=1):
        for i in range(w): self.px(x + i, y, c)

    def vline(self, x, y, h, c=1):
        for j in range(h): self.px(x, y + j, c)

    def fillrect(self, x, y, w, h, c=1):
        for i in range(w): self.vline(x + i, y, h, c)

    def line(self, x0, y0, x1, y1, c=1):
        steep = abs(y1 - y0) > abs(x1 - x0)
        if steep: x0, y0, x1, y1 = y0, x0, y1, x1
        if x0 > x1: x0, x1, y0, y1 = x1, x0, y1, y0
        dx = x1 - x0; dy = abs(y1 - y0); err = dx // 2
        ystep = 1 if y0 < y1 else -1
        while x0 <= x1:
            if steep: self.px(y0, x0, c)
            else: self.px(x0, y0, c)
            err -= dy
            if err < 0:
                y0 += ystep; err += dx
            x0 += 1

    def circle_helper(self, x0, y0, r, corner, c=1):
        f = 1 - r; ddx = 1; ddy = -2 * r; x = 0; y = r
        while x < y:
            if f >= 0:
                y -= 1; ddy += 2; f += ddy
            x += 1; ddx += 2; f += ddx
            if corner & 4: self.px(x0 + x, y0 + y, c); self.px(x0 + y, y0 + x, c)
            if corner & 2: self.px(x0 + x, y0 - y, c); self.px(x0 + y, y0 - x, c)
            if corner & 8: self.px(x0 - y, y0 + x, c); self.px(x0 - x, y0 + y, c)
            if corner & 1: self.px(x0 - y, y0 - x, c); self.px(x0 - x, y0 - y, c)

    def circle(self, x0, y0, r, c=1):
        f = 1 - r; ddx = 1; ddy = -2 * r; x = 0; y = r
        self.px(x0, y0 + r, c); self.px(x0, y0 - r, c); self.px(x0 + r, y0, c); self.px(x0 - r, y0, c)
        while x < y:
            if f >= 0:
                y -= 1; ddy += 2; f += ddy
            x += 1; ddx += 2; f += ddx
            for (a, b) in ((x, y), (y, x)):
                self.px(x0 + a, y0 + b, c); self.px(x0 - a, y0 + b, c)
                self.px(x0 + a, y0 - b, c); self.px(x0 - a, y0 - b, c)

    def fill_circle_helper(self, x0, y0, r, corners, delta, c=1):
        f = 1 - r; ddx = 1; ddy = -2 * r; x = 0; y = r; px = x; py = y
        delta += 1
        while x < y:
            if f >= 0:
                y -= 1; ddy += 2; f += ddy
            x += 1; ddx += 2; f += ddx
            if x < y + 1:
                if corners & 1: self.vline(x0 + x, y0 - y, 2 * y + delta, c)
                if corners & 2: self.vline(x0 - x, y0 - y, 2 * y + delta, c)
            if y != py:
                if corners & 1: self.vline(x0 + py, y0 - px, 2 * px + delta, c)
                if corners & 2: self.vline(x0 - py, y0 - px, 2 * px + delta, c)
                py = y
            px = x

    def fill_circle(self, x0, y0, r, c=1):
        self.vline(x0, y0 - r, 2 * r + 1, c)
        self.fill_circle_helper(x0, y0, r, 3, 0, c)

    def round_rect(self, x, y, w, h, r, c=1):
        r = min(r, min(w, h) // 2)
        self.hline(x + r, y, w - 2 * r, c); self.hline(x + r, y + h - 1, w - 2 * r, c)
        self.vline(x, y + r, h - 2 * r, c); self.vline(x + w - 1, y + r, h - 2 * r, c)
        self.circle_helper(x + r, y + r, r, 1, c)
        self.circle_helper(x + w - r - 1, y + r, r, 2, c)
        self.circle_helper(x + w - r - 1, y + h - r - 1, r, 4, c)
        self.circle_helper(x + r, y + h - r - 1, r, 8, c)

    def fill_round_rect(self, x, y, w, h, r, c=1):
        r = min(r, min(w, h) // 2)
        self.fillrect(x + r, y, w - 2 * r, h, c)
        self.fill_circle_helper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, c)
        self.fill_circle_helper(x + r, y + r, r, 2, h - 2 * r - 1, c)

    def fill_triangle(self, pts, c=1):
        """Scanline fill of a triangle with integer vertices (inclusive edges)."""
        (x0, y0), (x1, y1), (x2, y2) = sorted(pts, key=lambda p: p[1])
        def edge(xa, ya, xb, yb, y):
            return xa if yb == ya else xa + (xb - xa) * (y - ya) / (yb - ya)
        for y in range(y0, y2 + 1):
            xa = edge(x0, y0, x2, y2, y)
            xb = edge(x0, y0, x1, y1, y) if y <= y1 else edge(x1, y1, x2, y2, y)
            if y1 == y0 and y == y0: xb = x1
            lo, hi = sorted((xa, xb))
            for x in range(int(round(lo)), int(round(hi)) + 1):
                self.px(x, y, c)
