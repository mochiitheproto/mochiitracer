#pragma once

#include "../../Controller/ScreenSource.h"
#include "Font3x5.h"

// Patrón pa calibrar la orientación física (comando 't'): viendo la cabeza de
// frente, a la IZQUIERDA debe leerse una "L" roja y a la DERECHA una "R" verde,
// las dos derechas, con su marco. Si salen cambiadas o al revés: comando k<0-3>.
class CalScreen : public ScreenSource {
public:
    ScreenRGB GetPixel(uint8_t panel, uint8_t x, uint8_t y) override {
        if (x == 0 || x == 63 || y == 0 || y == 31) return {60, 60, 80};
        const int s = 4, gx0 = 32 - 6, gy0 = 16 - 10;  // glifo 3x5 escalado x4, centrado
        int gx = ((int)x - gx0) / s, gy = ((int)y - gy0) / s;
        if ((int)x >= gx0 && (int)y >= gy0 && gx < 3 && gy < 5 &&
            ((Font3x5::Glyph(panel ? 'R' : 'L')[gy] >> (2 - gx)) & 1)) {
            return panel ? ScreenRGB{40, 220, 90} : ScreenRGB{230, 50, 50};
        }
        return {0, 0, 0};
    }
};
