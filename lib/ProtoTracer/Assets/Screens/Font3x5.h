#pragma once

#include <stdint.h>

// Font 3x5 pa las pantallas: 5 renglones de 3 bits por glifo (bit 2 = columna
// izquierda). Mayúsculas, dígitos y poquita puntuación; lo demás sale en blanco.
namespace Font3x5 {
    static const uint8_t W = 3, H = 5, PITCH = 4;

    inline const uint8_t* Glyph(char c) {
        static const uint8_t blank[5] = {0, 0, 0, 0, 0};
        static const uint8_t az[26][5] = {
            {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7},  // A-E
            {7, 4, 6, 4, 4}, {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7}, {1, 1, 1, 5, 2},  // F-J
            {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7}, {5, 7, 7, 5, 5}, {6, 5, 5, 5, 5}, {2, 5, 5, 5, 2},  // K-O
            {6, 5, 6, 4, 4}, {2, 5, 5, 6, 3}, {6, 5, 6, 5, 5}, {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2},  // P-T
            {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5}, {5, 5, 2, 2, 2},  // U-Y
            {7, 1, 2, 4, 7},                                                                      // Z
        };
        static const uint8_t d09[10][5] = {
            {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {6, 1, 2, 4, 7}, {6, 1, 2, 1, 6}, {5, 5, 7, 1, 1},
            {7, 4, 6, 1, 6}, {3, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 6},
        };
        static const uint8_t pct[5] = {5, 1, 2, 4, 5};
        static const uint8_t colon[5] = {0, 2, 0, 2, 0};
        static const uint8_t dot[5] = {0, 0, 0, 0, 2};
        static const uint8_t bang[5] = {2, 2, 2, 0, 2};
        static const uint8_t dash[5] = {0, 0, 7, 0, 0};
        static const uint8_t lpar[5] = {1, 2, 2, 2, 1};
        static const uint8_t rpar[5] = {4, 2, 2, 2, 4};

        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z') return az[c - 'A'];
        if (c >= '0' && c <= '9') return d09[c - '0'];
        switch (c) {
            case '%': return pct;
            case ':': return colon;
            case '.': return dot;
            case '!': return bang;
            case '-': return dash;
            case '(': return lpar;
            case ')': return rpar;
            default:  return blank;
        }
    }

    inline uint8_t Width(const char* s) {
        uint8_t n = 0;
        while (s[n]) n++;
        return n ? (uint8_t)(n * PITCH - 1) : 0;
    }

    // Llama plot(x, y, i) por cada pixel prendido del texto (i = índice del
    // caracter, pa colorear por letra). Sólo dibuja los primeros maxChars.
    template <typename Plot>
    void Draw(const char* s, int x0, int y0, Plot plot, int maxChars = 255) {
        for (int i = 0; s[i] && i < maxChars; i++) {
            const uint8_t* g = Glyph(s[i]);
            for (int gy = 0; gy < H; gy++)
                for (int gx = 0; gx < W; gx++)
                    if ((g[gy] >> (2 - gx)) & 1) plot(x0 + i * PITCH + gx, y0 + gy, i);
        }
    }
}
