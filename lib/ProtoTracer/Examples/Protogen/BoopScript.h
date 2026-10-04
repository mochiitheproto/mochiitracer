#pragma once

#include <stdint.h>
#include "BoopPresets.h"

// Dedo de mentira pa probar el gesto en la cabeza sin tocarla (comando serial g<N>).
// Reproduce un preset de tools/oledsim/boop_presets.json (los MISMOS números que el
// simulador, vía BoopPresets.h generado) y sólo reemplaza lo que ve el gesto:
//   boopGesture.Update(boopScript.Active() ? boopScript.Sample(now) : IsBooped(), now)
// SelectFace sigue con el sensor de verdad. Primero LEAD_MS sin boop (pa que el gesto
// se arme: ARM_QUIET_MS = 400), luego el patrón, luego TAIL_MS sin boop pa que el gesto
// termine solito (un boop sencillo tarda GAP_MAX_MS = 400 en cerrarse), y se apaga solo.
// Sin Arduino a propósito: el mismo header corre en las pruebas de host.
class BoopScript {
public:
    static constexpr uint32_t LEAD_MS = 500;
    static constexpr uint32_t TAIL_MS = 1000;

    // n = 1..BoopPresets::COUNT (el N de g<N>). false si no existe.
    bool Start(uint8_t n, uint32_t nowMs) {
        if (n < 1 || n > BoopPresets::COUNT) return false;
        preset = n;
        t0 = nowMs;
        return true;
    }
    void Stop() { preset = 0; }
    bool Active() const { return preset != 0; }

    // Lo que "ve" el sensor de mentira en nowMs (una vez por frame, mientras Active()).
    // Al acabarse la cola se apaga solo y regresa false.
    bool Sample(uint32_t nowMs) {
        if (!preset) return false;
        // con signo: Start() puede haber leído millis() un pelito después que este frame
        int32_t t = (int32_t)(nowMs - t0) - (int32_t)LEAD_MS;
        if (t < 0) return false;
        const BoopPresets::Preset& p = BoopPresets::presets[preset - 1];
        for (uint8_t i = 0; i < p.count; i++) {
            if (t < (int32_t)p.ms[i]) return (i & 1) == 0;  // pares = prendido
            t -= p.ms[i];
        }
        if (t >= (int32_t)TAIL_MS) preset = 0;
        return false;
    }

    // Duración total de un preset (lead + patrón + cola), pa las pruebas y la captura.
    static uint32_t TotalMs(uint8_t n) {
        if (n < 1 || n > BoopPresets::COUNT) return 0;
        uint32_t total = LEAD_MS + TAIL_MS;
        const BoopPresets::Preset& p = BoopPresets::presets[n - 1];
        for (uint8_t i = 0; i < p.count; i++) total += p.ms[i];
        return total;
    }

private:
    uint8_t preset = 0;   // 0 = apagado; si no, el N de g<N>
    uint32_t t0 = 0;
};
