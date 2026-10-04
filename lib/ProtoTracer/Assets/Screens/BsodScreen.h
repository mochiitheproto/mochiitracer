#pragma once

#include "../../Controller/ScreenSource.h"
#include "Font3x5.h"
#include "Especie.h"

// Pantallazo azul a lo ancho de los DOS paneles (128x32, vista de frente):
// izquierda ":(" grande + "% COMPLETE"; derecha el mensaje y un QR de verdad
// (versión 1, 21x21) que al escanearlo dice "boop :3". Safe zone respetada.
class BsodScreen : public ScreenSource {
public:
    // Cuenta del % a saltos irregulares como el de Windows, en ms desde que
    // entró al slot. Después de COUNT_MS se queda en 100.
    static const uint32_t COUNT_MS = 8600;

    static uint8_t Percent(uint32_t t) {
        static const uint16_t at[] = {0, 600, 1100, 1500, 2600, 2900, 3400, 4800, 5200, 5500, 6400, 6900, 7800, 8600};
        static const uint8_t  pc[] = {0, 7,   15,   22,   23,   38,   47,   51,   64,   72,   86,   93,   99,   100};
        uint8_t p = 0;
        for (uint8_t i = 0; i < sizeof(at) / sizeof(at[0]); i++)
            if (t >= at[i]) p = pc[i];
        return p;
    }

    void Render(uint8_t percent) {
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 128; x++) buf[y][x] = kBlue;

        // ":(" — dos puntos de 3x3 y un paréntesis curvo de 3 px de grueso
        Rect(7, 7, 3, 3, kWhite);
        Rect(7, 14, 3, 3, kWhite);
        for (int y = 3; y <= 21; y++) {
            int dy = y - 12;
            int x = 15 + (dy * dy * 4 + 40) / 81;  // más a la izquierda en medio
            Rect(x, y, 3, 1, kWhite);
        }

        char line[16];
        int n = 0;
        if (percent >= 100) line[n++] = '1';
        if (percent >= 10) line[n++] = (char)('0' + (percent / 10) % 10);
        line[n++] = (char)('0' + percent % 10);
        const char* rest = "% COMPLETE";
        for (int i = 0; rest[i]; i++) line[n++] = rest[i];
        line[n] = 0;
        Text(line, 4, 25);

        // panel derecho: mensaje (YOUR PROTOGEN / PRIMAGEN NEEDS A BOOP) + QR con 1 px de margen blanco
        Text("YOUR", 64 + 2, 4);
        Text(ESPECIE_NOMBRE, 64 + 2, 11);
        Text("NEEDS A", 64 + 2, 18);
        Text("BOOP", 64 + 2, 25);

        static const uint32_t qr[21] = {
            0x1FD77F, 0x104741, 0x17545D, 0x17445D, 0x174F5D, 0x105341, 0x1FD57F, 0x000D00, 0x146825, 0x130ECD, 0x0EDFB1,
            0x05A6F9, 0x07EFFB, 0x00183D, 0x1FDC71, 0x10491B, 0x174861, 0x174EF0, 0x175FBF, 0x1042C0, 0x1FD7F9,
        };
        const int qx = 64 + 39, qy = 5;
        Rect(qx - 1, qy - 1, 23, 23, kWhite);
        for (int r = 0; r < 21; r++)
            for (int c = 0; c < 21; c++)
                if ((qr[r] >> (20 - c)) & 1) buf[qy + r][qx + c] = kBlack;
    }

    ScreenRGB GetPixel(uint8_t panel, uint8_t x, uint8_t y) override { return buf[y][panel * 64 + x]; }

private:
    ScreenRGB buf[32][128];

    static constexpr ScreenRGB kBlue  = {0, 120, 215};  // el azul de Windows 10
    static constexpr ScreenRGB kWhite = {235, 240, 250};
    static constexpr ScreenRGB kBlack = {0, 0, 0};

    void Rect(int x0, int y0, int w, int h, ScreenRGB c) {
        for (int y = y0; y < y0 + h; y++)
            for (int x = x0; x < x0 + w; x++)
                if (x >= 0 && x < 128 && y >= 0 && y < 32) buf[y][x] = c;
    }

    void Text(const char* s, int x0, int y0) {
        Font3x5::Draw(s, x0, y0, [&](int x, int y, int) {
            if (x >= 0 && x < 128 && y >= 0 && y < 32) buf[y][x] = kWhite;
        });
    }
};
