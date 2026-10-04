// GENERADO por tools/oledsim/gen_boop_presets.py desde tools/oledsim/boop_presets.json:
// no se edita a mano (edita el JSON y corre el script). La misma tabla que usa el simulador.
#pragma once

#include <stdint.h>

namespace BoopPresets {
static constexpr uint8_t COUNT = 6;
static constexpr uint8_t MAX_SEGMENTS = 7;

// ms alternando PRENDIDO/apagado, empezando prendido (prendido = IsBooped() true).
struct Preset {
    uint8_t count;
    uint16_t ms[MAX_SEGMENTS];
};

// índice = N - 1 del comando g<N>
static constexpr Preset presets[COUNT] = {
    {3, {150, 150, 1200}},  // g1 doble: boop + boooop: tap, release, hold 1.2 s (fires at 0.8 s of hold)
    {1, {150}},  // g2 sencillo: plain single boop (strangers at a con)
    {3, {150, 150, 200}},  // g3 doble_tap: normal double tap: 2nd press let go long before the hold
    {3, {150, 150, 500}},  // g4 suelta: almost: 2nd press let go at 0.5 s (hold needs 0.8 s)
    {1, {2000}},  // g5 abrazo: hug / resting hand: one 2 s contact
    {7, {150, 150, 1200, 1000, 150, 150, 1200}},  // g6 seguido: doble, 1 s quiet, doble again (two face changes)
};

// Sólo pa las pruebas en host (el firmware no las usa, el linker las tira).
static constexpr const char* names[COUNT] = {"doble", "sencillo", "doble_tap", "suelta", "abrazo", "seguido"};
// Eventos esperados (BoopGesture::Name), separados por espacio.
static constexpr const char* expect[COUNT] = {
    "tap1 hold FIRE",  // g1 doble
    "tap1 boop-sencillo",  // g2 sencillo
    "tap1 hold cancel:solto",  // g3 doble_tap
    "tap1 hold cancel:solto",  // g4 suelta
    "cancel:tap-largo",  // g5 abrazo
    "tap1 hold FIRE tap1 hold FIRE",  // g6 seguido
};
}  // namespace BoopPresets
