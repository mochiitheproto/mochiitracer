#pragma once

#include <stdint.h>

// Pantalla que se dibuja DIRECTO a los paneles, sin pasar por la cámara 3D.
// Coordenadas de quien ve la cabeza de frente: panel 0 = izquierda, 1 = derecha;
// x 0..63 de izquierda a derecha, y 0..31 de arriba a abajo. Así el texto sale
// derecho en los dos paneles (el modo normal espejea uno pa que la cara sea
// simétrica). Sin Arduino a propósito: se compila y se previsualiza en host.
struct ScreenRGB {
    uint8_t r, g, b;
};

class ScreenSource {
public:
    virtual ScreenRGB GetPixel(uint8_t panel, uint8_t x, uint8_t y) = 0;

protected:
    ~ScreenSource() = default;  // nunca se borra por la base
};
