#pragma once

#include "../../Controller/ScreenSource.h"

// Pantalla apagada (comando 'o0'): negro total, ni el brillo 0 (5/255) se ve.
// Sólo vive en RAM: 'o1' o un reinicio la regresan.
class BlankScreen : public ScreenSource {
public:
    ScreenRGB GetPixel(uint8_t, uint8_t, uint8_t) override { return {0, 0, 0}; }
};
