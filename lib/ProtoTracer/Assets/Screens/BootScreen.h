#pragma once

#include "../../Controller/ScreenSource.h"
#include "Font3x5.h"
#include "Especie.h"

// Arranque (no es slot): "PROTOGEN OS" ("PRIMAGEN OS" con ESPECIE_PRIMAGEN, ver Especie.h) a
// máquina de escribir con cursor, una estrellita que destella, barra arcoíris pastel con
// brillito que la recorre y que se atora en 99% (como todas), flash al 100 y fade a la cara.
// Paleta arcoíris pastel. Igual en los dos paneles y con el texto derecho.
// Todo dentro de la safe zone X=2..61 / Y=2..29.
class BootScreen : public ScreenSource {
public:
    static const uint32_t TYPE_MS  = 1100;  // una letra cada 100 ms
    static const uint32_t BAR_AT   = 1300;
    static const uint32_t BAR_MS   = 2000;
    static const uint32_t FLASH_MS = 250;
    static const uint32_t FADE_MS  = 500;
    static const uint32_t TOTAL_MS = BAR_AT + BAR_MS + FLASH_MS + FADE_MS;  // 4050

    void Render(uint32_t t) {
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 64; x++) buf[y][x] = {0, 0, 0};

        // texto: la especie en blanco, "OS" rosa
        const char* txt = ESPECIE_NOMBRE " OS";
        const int n = 11, tx = 10, ty = 5;
        int shown = t >= TYPE_MS ? n : (int)(t * n / TYPE_MS) + 1;
        Font3x5::Draw(txt, tx, ty, [&](int x, int y, int i) {
            Set(x, y, i >= 9 ? kPink : kWhite);
        }, shown);

        // cursor parpadeando hasta que sale la estrella
        if (t < TYPE_MS + 150 && (t / 200) % 2 == 0)
            Rect(tx + (shown < n ? shown : n) * Font3x5::PITCH, ty, 3, 5, kCyan);
        if (t >= TYPE_MS + 150) Star(57, 7, t);

        if (t >= BAR_AT) Bar(t - BAR_AT);

        // fade final a negro (la cara aparece con su propio soft-start)
        uint32_t fadeAt = BAR_AT + BAR_MS + FLASH_MS;
        if (t > fadeAt) {
            uint32_t k = t - fadeAt >= FADE_MS ? 0 : 255 - (t - fadeAt) * 255 / FADE_MS;
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 64; x++) {
                    ScreenRGB& c = buf[y][x];
                    c = {(uint8_t)(c.r * k / 255), (uint8_t)(c.g * k / 255), (uint8_t)(c.b * k / 255)};
                }
        }
    }

    ScreenRGB GetPixel(uint8_t, uint8_t x, uint8_t y) override { return buf[y][x]; }

    // Progreso falso en % pa u ms de barra: rápido, se atora, sigue, se queda
    // en 99 un buen rato y al final brinca a 100.
    static int Progress(uint32_t u) {
        float f = (float)u / BAR_MS;
        if (f < 0.35f) { float s = f / 0.35f; return (int)(62.0f * (1.0f - (1.0f - s) * (1.0f - s))); }
        if (f < 0.45f) return 62 + (int)((u / 90) % 2);
        if (f < 0.72f) { float s = (f - 0.45f) / 0.27f; return 62 + (int)(37.0f * s * s * (3.0f - 2.0f * s)); }
        if (f < 0.97f) return 99;
        return 100;
    }

private:
    ScreenRGB buf[32][64];

    static constexpr ScreenRGB kWhite  = {230, 230, 240};
    static constexpr ScreenRGB kPink   = {255, 91, 168};
    static constexpr ScreenRGB kCyan   = {127, 212, 245};
    static constexpr ScreenRGB kYellow = {255, 217, 61};
    static constexpr ScreenRGB kFrame  = {70, 70, 90};

    void Set(int x, int y, ScreenRGB c) {
        if (x >= 0 && x < 64 && y >= 0 && y < 32) buf[y][x] = c;
    }

    void Rect(int x0, int y0, int w, int h, ScreenRGB c) {
        for (int y = y0; y < y0 + h; y++)
            for (int x = x0; x < x0 + w; x++) Set(x, y, c);
    }

    static ScreenRGB Mix(ScreenRGB a, ScreenRGB b, int k) {  // k 0..256 hacia b
        return {(uint8_t)(a.r + (b.r - a.r) * k / 256), (uint8_t)(a.g + (b.g - a.g) * k / 256),
                (uint8_t)(a.b + (b.b - a.b) * k / 256)};
    }

    // arcoíris pastel, interpolado de 0..255
    static ScreenRGB Rainbow(int p) {
        static const ScreenRGB stops[6] = {
            {255, 154, 162}, {255, 179, 128}, {255, 224, 102}, {168, 230, 161}, {143, 211, 244}, {195, 168, 240},
        };
        int seg = p * 5 / 256, k = (p * 5 % 256);
        if (seg >= 5) return stops[5];
        return Mix(stops[seg], stops[seg + 1], k);
    }

    // destello: alterna cruz corta / cruz larga con diagonales
    void Star(int cx, int cy, uint32_t t) {
        bool big = (t / 180) % 2 == 0;
        ScreenRGB dim = {200, 160, 40};
        Set(cx, cy, kYellow);
        for (int d = 1; d <= (big ? 2 : 1); d++) {
            ScreenRGB c = d == 1 ? kYellow : dim;
            Set(cx + d, cy, c); Set(cx - d, cy, c); Set(cx, cy + d, c); Set(cx, cy - d, c);
        }
        if (big) { Set(cx + 1, cy + 1, dim); Set(cx - 1, cy - 1, dim); Set(cx + 1, cy - 1, dim); Set(cx - 1, cy + 1, dim); }
    }

    void Bar(uint32_t u) {
        const int x0 = 6, y0 = 15, w = 52, h = 7;      // marco
        const int ix = 8, iy = 17, iw = 48, ih = 3;    // relleno
        bool flash = u >= BAR_MS;
        int p = flash ? 100 : Progress(u);

        ScreenRGB frame = flash ? kWhite : kFrame;
        for (int x = x0; x < x0 + w; x++) { Set(x, y0, frame); Set(x, y0 + h - 1, frame); }
        for (int y = y0; y < y0 + h; y++) { Set(x0, y, frame); Set(x0 + w - 1, y, frame); }

        int filled = p * iw / 100;
        int shine = (int)(u % 700) * (iw + 12) / 700 - 6;  // brillito que corre
        for (int x = 0; x < filled; x++) {
            ScreenRGB c = Rainbow(x * 255 / (iw - 1));
            int d = x - shine < 0 ? shine - x : x - shine;
            if (d < 4) c = Mix(c, kWhite, (4 - d) * 40);
            if (flash && u - BAR_MS < 110) c = kWhite;  // flashazo al llegar a 100
            for (int y = iy; y < iy + ih; y++) Set(ix + x, y, c);
        }

        char pctTxt[5];
        int len = 0;
        if (p >= 100) pctTxt[len++] = '1';
        if (p >= 10) pctTxt[len++] = (char)('0' + (p / 10) % 10);
        pctTxt[len++] = (char)('0' + p % 10);
        pctTxt[len++] = '%';
        pctTxt[len] = 0;
        Font3x5::Draw(pctTxt, (64 - Font3x5::Width(pctTxt)) / 2, 24, [&](int x, int y, int) { Set(x, y, kCyan); });
    }
};
