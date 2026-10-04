// Prueba en host del dedo de mentira (serial g<N>): el BoopGesture.h REAL alimentado por
// BoopScript.h con los presets de BoopPresets.h (generado de boop_presets.json), en el mismo
// orden que ProtogenHUB75Project::Update():
//   Update(boopScript.Active() ? boopScript.Sample(now) : IsBooped(), now)
// con IsBooped() = false (nadie tocando la nariz). Cada preset corre con frames de 12 ms
// (~83 FPS) y con 12 ms + un frame de 39 ms (uno que manda la OLED) cada 5, en todas las
// fases, y tiene que dar los eventos esperados del JSON, una sola vez, y apagarse solo.
// Correr con tools/oledsim/tests/boop_inject.sh
#include <cstdio>
#include <cstring>
#include <string>

#include "BoopGesture.h"
#include "BoopScript.h"

struct Result {
    std::string events;  // nombres de los eventos que se loguean, separados por espacio
    bool ended = false;  // el script se apagó solo
    uint32_t endMs = 0;  // cuándo (desde el g<N>)
    uint32_t fireMs = 0; // primer FIRE (desde el g<N>), 0 = no hubo
};

// slowEvery = 0: todos los frames de 12 ms; si no, cada slowEvery-ésimo frame dura 39 ms.
// phase = corrimiento del reloj (fase del muestreo); slowPhase = cuál frame del ciclo es el lento.
// stopAt = manda g0 a esos ms del g<N> (0 = nunca).
static Result play(uint8_t n, int slowEvery, uint32_t phase, int slowPhase, uint32_t stopAt = 0) {
    BoopGesture gesture;
    BoopScript script;
    Result r;
    uint32_t now = 4050 + phase;  // justo al terminar el arranque
    gesture.Reset(now);
    int k = slowPhase;
    auto step = [&]() {
        k++;
        now += (slowEvery && k % slowEvery == 0) ? 39 : 12;
    };
    for (int i = 0; i < 200; i++) {  // un rato sin nadie: el gesto se arma
        gesture.Update(false, now);
        step();
    }
    // RunCommand corre antes del bloque del gesto y lee millis() un poquito después de boopNow
    uint32_t start = now + 1;
    script.Start(n, start);
    bool stopped = false;
    while (true) {
        int32_t el = (int32_t)(now - start);  // el 1er frame va 1 ms ANTES del Start (ver arriba)
        if (stopAt && !stopped && el >= (int32_t)stopAt) {
            script.Stop();
            stopped = true;
        }
        bool in = script.Active() ? script.Sample(now) : false;
        BoopGesture::Event e = gesture.Update(in, now);
        if (BoopGesture::Loggable(e)) {
            if (!r.events.empty()) r.events += ' ';
            r.events += BoopGesture::Name(e);
            if (e == BoopGesture::FIRE && !r.fireMs) r.fireMs = el;
        }
        if (!script.Active() && !r.ended) {
            r.ended = true;
            r.endMs = el;
        }
        if (r.ended && el > (int32_t)r.endMs + 3000) break;  // y luego nada más pasa
        if (el > 30000) break;
        step();
    }
    return r;
}

int main() {
    int fails = 0, runs = 0;
    printf("preset      eventos esperados                     12ms   12+39ms  FIRE(ms)  fin(ms)/total\n");
    for (uint8_t n = 1; n <= BoopPresets::COUNT; n++) {
        const char *want = BoopPresets::expect[n - 1];
        uint32_t total = BoopScript::TotalMs(n);
        int okFast = 0, okSlow = 0, nFast = 0, nSlow = 0;
        uint32_t fireMin = 0xFFFFFFFF, fireMax = 0, endMax = 0;
        for (int variant = 0; variant < 2; variant++) {
            for (uint32_t phase = 0; phase < 12; phase++) {
                for (int sp = 0; sp < (variant ? 5 : 1); sp++) {
                    Result r = play(n, variant ? 5 : 0, phase, sp);
                    runs++;
                    // termina solo, a tiempo (a lo mucho un frame lento tarde)
                    bool ok = r.events == want && r.ended && r.endMs >= total && r.endMs <= total + 39;
                    if (ok) (variant ? okSlow : okFast)++;
                    (variant ? nSlow : nFast)++;
                    if (r.fireMs) {
                        if (r.fireMs < fireMin) fireMin = r.fireMs;
                        if (r.fireMs > fireMax) fireMax = r.fireMs;
                    }
                    if (r.endMs > endMax) endMax = r.endMs;
                    if (!ok) {
                        fails++;
                        if (fails <= 12)
                            printf("  FALLA g%u %s %s fase %u lento %d: \"%s\" (fin %s %u ms, total %u)\n", n,
                                   BoopPresets::names[n - 1], variant ? "12+39ms" : "12ms", phase, sp, r.events.c_str(),
                                   r.ended ? "a los" : "NUNCA", r.endMs, total);
                    }
                }
            }
        }
        char fire[32] = "-";
        if (fireMax) snprintf(fire, sizeof(fire), "%u-%u", fireMin, fireMax);
        printf("g%u %-9s %-37s %2d/%-2d  %2d/%-2d   %-9s %u/%u\n", n, BoopPresets::names[n - 1], want, okFast, nFast,
               okSlow, nSlow, fire, endMax, total);
    }

    // g0 a media jugada: el gesto vuelve a ver el sensor real (nadie) = soltó
    for (int variant = 0; variant < 2; variant++) {
        Result r = play(1, variant ? 5 : 0, 5, 2, 1000);  // lead 500 + 150 + 150 → a los 1000 lleva 200 sostenido
        runs++;
        bool ok = r.events == std::string("tap1 hold cancel:solto") && r.ended && r.endMs <= 1000 + 39;
        printf("g1 y g0 a 1 s (%s): \"%s\" %s\n", variant ? "12+39ms" : "12ms", r.events.c_str(), ok ? "ok" : "FALLA");
        if (!ok) fails++;
    }

    // números fuera de la tabla
    BoopScript s;
    bool okRange = !s.Start(0, 0) && !s.Start(BoopPresets::COUNT + 1, 0) && !s.Active() && !s.Sample(123);
    printf("g0/g%u no arrancan nada: %s\n", BoopPresets::COUNT + 1, okRange ? "ok" : "FALLA");
    if (!okRange) fails++;

    printf("%d corridas, %s\n", runs, fails ? "HAY FALLAS" : "todo como el JSON");
    return fails ? 1 : 0;
}
