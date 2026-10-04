#pragma once

#include <stdint.h>

// Gesto "boop + boooop": tap corto, suelta, y el 2o tap se SOSTIENE → FIRE
// (siguiente cara). Dispara al cumplir el hold, sin esperar a que suelte.
// Sin Arduino a propósito: se compila y se prueba en host con g++.
// Update(booped, millis()) una vez por frame. Todo el tiempo va en restas
// uint32_t (aguanta el wrap de millis). Cero memoria dinámica.
//
// La ÚNICA secuencia que dispara:
//   suelto >= ARM_QUIET | tap TAP_MIN..TAP_MAX | suelto RELEASE..GAP_MAX | press >= HOLD
// Un bajón < RELEASE_MS dentro de un press es chatter del sensor (no soltada) y
// se puentea mientras los bajones no sumen más de DROP_BUDGET_MS. Cualquier
// otra cosa cancela y regresa a esperar silencio real (ARM_QUIET_MS, o
// LONG_QUIET_MS tras un contacto largo); por eso un hold dispara una sola vez.
class BoopGesture {
public:
    // Silencio pa armar (también es el cooldown post-FIRE/cancel). Mata ráfagas
    // (gente booopeando, pelo, capucha: huecos < 400) y el tap-tap-hold.
    static constexpr uint32_t ARM_QUIET_MS   = 400;
    // Tap real: el dedo pasa >60 ms en rango. Glitches de 1-4 frames no llegan.
    static constexpr uint32_t TAP_MIN_MS     = 60;
    // Más ya es mano recargada / abrazo, no tap.
    static constexpr uint32_t TAP_MAX_MS     = 350;
    // Bajón más corto = chatter (no soltó). También es el hueco mínimo entre taps.
    static constexpr uint32_t RELEASE_MS     = 80;
    // Soltada del tap → inicio del hold. Más = boop sencillo.
    static constexpr uint32_t GAP_MAX_MS     = 400;
    // Hold del 2o press: "deja el dedo un segundo". Más largo que un "boooop"
    // juguetón de ~0.5 s. Peor caso tap→FIRE: TAP_MAX + ~205 + HOLD = 1.35 s.
    static constexpr uint32_t HOLD_MS        = 800;
    // Bajones puenteados que se toleran por press, sumados (20% del hold): un
    // par de temblores sí, un parpadeo no (a 50% de duty se lo acaba en ~320 ms).
    static constexpr uint32_t DROP_BUDGET_MS = 160;
    // OJO MinFilter: la base del APDS es el mínimo de ~2 s de muestras, así que
    // una mano que se queda se "absorbe" e IsBooped() cae CON la mano puesta. Tras
    // un contacto largo ya no sabemos si soltó: re-armar pide LONG_QUIET_MS (más
    // que la ventana), si no el bamboleo de la mano recargada arma otro gesto.
    // Sin FIRE: lo más pronto que absorbe es ~1.43 s del hold (tap largo + hueco
    // sin muestra), así que 1.3 s cubre todo. Tras FIRE se le da más cuerda al que
    // se queda esperando ver el cambio: lo típico absorbe a >= 1.8 s del hold.
    static constexpr uint32_t LONG_PRESS_MS  = 1300;
    static constexpr uint32_t LONG_HOLD_MS   = 1600;  // contado desde el inicio del hold que disparó
    static constexpr uint32_t LONG_QUIET_MS  = 2500;
    // Hueco entre Update() más largo = frame atorado (I2C/ResetI2CBus/EEPROM):
    // el gesto en curso ya no es confiable.
    static constexpr uint32_t STALL_MS       = 100;
    // Muestras mínimas (además del tiempo): un frame con timeout I2C dura ~70 ms
    // y lee 156, así que el tiempo solo no basta.
    static constexpr uint8_t  MIN_SAMPLES      = 3;   // tap y soltada
    static constexpr uint8_t  HOLD_MIN_SAMPLES = 16;  // hold (prom. <50 ms/frame)

    enum Event : uint8_t {
        NONE = 0,
        TAP1,               // 1er tap válido (DurMs = largo del tap)
        HOLD_START,         // arrancó el 2o press (DurMs = gap)
        FIRE,               // hold cumplido → siguiente cara (DurMs = hold)
        GLITCH,             // press de < MIN_SAMPLES frames (ruido; no se loguea)
        CANCEL_TAP_SHORT,   // 1er press de >= MIN_SAMPLES frames pero < TAP_MIN (tampoco; sale como Cause())
        CANCEL_TAP_LONG,    // 1er press muy largo (abrazo, recargón)
        CANCEL_TAP_NOISY,   // 1er press con demasiados bajones (ráfaga)
        CANCEL_GAP_TIMEOUT, // nunca llegó el 2o: boop sencillo (DurMs = gap)
        CANCEL_TAP2_SHORT,  // soltó antes de HOLD: doble tap normal (DurMs = hasta el último frame alto, < HOLD)
        CANCEL_HOLD_BROKEN, // hold con demasiados bajones (DurMs = hold)
        CANCEL_HOLD_FEW,    // hold con < HOLD_MIN_SAMPLES frames: cadena de frames lentos
        CANCEL_STALL,       // frame atorado (> STALL_MS) a media jugada (DurMs = hueco)
        IGNORED,            // press con pinta de tap mientras estaba desarmado (DurMs = silencio que llevaba; Cause() = qué desarmó)
        IGNORED_LOCK        // igual, pero en el bloqueo tras contacto largo
    };

    // Qué vale la pena mandar al serial: en ruido de pulsos GLITCH y tap-corto
    // serían cientos por hora; si se comen un gesto, sale el "ignorado" con su causa.
    static bool Loggable(Event e) { return e != NONE && e != GLITCH && e != CANCEL_TAP_SHORT; }

    // Desarma: pide ARM_QUIET_MS de silencio REAL desde nowMs. Pa boot y
    // mientras el sensor esté apagado en el menú (IsBooped() se congela).
    void Reset(uint32_t nowMs) {
        state = WAIT_QUIET;
        cause = NONE;
        quietRef = nowMs;
        lastUpdMs = nowMs;
        started = true;
        lowPending = false;
        inContact = false;
        longRun = false;
        firedRun = false;
        warned = false;
    }

    Event Update(bool booped, uint32_t now) {
        if (!started) Reset(now);
        uint32_t dt = now - lastUpdMs;
        lastUpdMs = now;
        if (dt > STALL_MS) {
            // el hueco nunca cuenta como gap ni hold
            bool active = state >= TAP;
            Quiet(state == WAIT_QUIET ? cause : CANCEL_STALL, now, false);
            return active ? Emit(CANCEL_STALL, dt, 0) : NONE;
        }

        if (state == WAIT_QUIET) {
            // se arma en el mismo frame, si no un boop que llega justo se pierde
            if (now - quietRef < (longRun ? LONG_QUIET_MS : ARM_QUIET_MS)) return Disarmed(booped, now);
            state = ARMED;
            longRun = false;
        }

        switch (state) {
        case ARMED:
            if (booped) StartPress(TAP, now);
            return NONE;

        case TAP:
            if (booped) {
                if (Bridge(now)) return Stop(CANCEL_TAP_NOISY, now - pressStart, now, true);
                if (now - pressStart > TAP_MAX_MS) return Stop(CANCEL_TAP_LONG, now - pressStart, now, true);
                return NONE;
            }
            if (!Released(now)) return NONE;
            {
                uint32_t len = lowStart - pressStart;
                if (highCount < MIN_SAMPLES) return Stop(GLITCH, len, lowStart, false);
                if (len < TAP_MIN_MS) return Stop(CANCEL_TAP_SHORT, len, lowStart, false);
                if (len > TAP_MAX_MS) return Stop(CANCEL_TAP_LONG, len, lowStart, false);
                state = GAP;
                return Emit(TAP1, len, dropSum);
            }

        case GAP: {
            // timeout primero: un press tardío (o tras un frame lento) no es el 2o tap
            uint32_t gap = now - lowStart;
            if (gap > GAP_MAX_MS) {
                if (booped) pressStart = now;  // el press tardío es un contacto nuevo
                Quiet(CANCEL_GAP_TIMEOUT, booped ? now : lowStart, booped);
                return Emit(CANCEL_GAP_TIMEOUT, gap, 0);
            }
            if (!booped) return NONE;
            StartPress(HOLD, now);
            return Emit(HOLD_START, gap, 0);
        }

        case HOLD:
            if (booped) {
                if (Bridge(now)) return Stop(CANCEL_HOLD_BROKEN, now - pressStart, now, true);
                // sólo en frame alto: nunca dispara a media caída
                if (now - pressStart >= HOLD_MS) {
                    if (highCount < HOLD_MIN_SAMPLES) return Stop(CANCEL_HOLD_FEW, now - pressStart, now, true);
                    return Stop(FIRE, now - pressStart, now, true);
                }
                return NONE;
            }
            if (!Released(now)) return NONE;
            // lo que se VIO sostenido (hasta el último frame alto), mismo criterio que FIRE
            return Stop(CANCEL_TAP2_SHORT, lastHighMs - pressStart, lowStart, false);

        default:
            return NONE;
        }
    }

    // Pa la pantallita de adentro (sólo lectura, no cambia el gesto): en qué parte
    // va y cuánto lleva sostenido el 2o press. Mismo orden que State.
    enum Stage : uint8_t { STAGE_WAIT = 0, STAGE_ARMED, STAGE_TAP, STAGE_GAP, STAGE_HOLD };
    Stage GetStage() const { return (Stage)state; }
    uint32_t HoldMs(uint32_t now) const { return state == HOLD ? now - pressStart : 0; }

    uint32_t DurMs() const { return durMs; }    // duración asociada al último evento
    uint32_t DropMs() const { return dropMs; }  // bajones puenteados del press del último evento (0 si no aplica)
    Event Cause() const { return cause; }       // qué causó el desarme actual (NONE = Reset)

    static const char* Name(Event e) {
        switch (e) {
        case TAP1:               return "tap1";
        case HOLD_START:         return "hold";
        case FIRE:               return "FIRE";
        case GLITCH:             return "glitch";
        case CANCEL_TAP_SHORT:   return "cancel:tap-corto";
        case CANCEL_TAP_LONG:    return "cancel:tap-largo";
        case CANCEL_TAP_NOISY:   return "cancel:tap-ruidoso";
        case CANCEL_GAP_TIMEOUT: return "boop-sencillo";
        case CANCEL_TAP2_SHORT:  return "cancel:solto";
        case CANCEL_HOLD_BROKEN: return "cancel:hold-ruidoso";
        case CANCEL_HOLD_FEW:    return "cancel:hold-pocas-muestras";
        case CANCEL_STALL:       return "cancel:frame-atorado";
        case IGNORED:            return "ignorado";
        case IGNORED_LOCK:       return "ignorado:bloqueo";
        case NONE:               return "reset";  // sólo sale como Cause()
        default:                 return "?";
        }
    }

private:
    enum State : uint8_t { WAIT_QUIET = 0, ARMED, TAP, GAP, HOLD };

    uint8_t  state      = WAIT_QUIET;
    Event    cause      = NONE;
    bool     started    = false;
    bool     lowPending = false;
    bool     inContact  = false;  // desarmado: sigue habiendo mano (bajones cortos no la cortan)
    bool     longRun    = false;  // hubo contacto largo: re-armar pide LONG_QUIET_MS
    bool     firedRun   = false;  // el contacto actual es el hold que disparó
    bool     warned     = false;  // ya avisó "ignorado" en este desarme
    uint8_t  highCount  = 0;   // frames altos del press/contacto actual (satura)
    uint8_t  lowCount   = 0;   // frames bajos del bajón actual (satura)
    uint32_t pressStart = 0;   // flanco del press/contacto actual
    uint32_t lowStart   = 0;   // 1er frame bajo (y luego inicio del gap)
    uint32_t lastHighMs = 0;   // último frame alto del press
    uint32_t dropSum    = 0;
    uint32_t quietRef   = 0;   // desde aquí se cuenta el silencio
    uint32_t quietPrev  = 0;   // silencio que llevaba al llegar el contacto actual
    uint32_t lastUpdMs  = 0;
    uint32_t durMs      = 0;
    uint32_t dropMs     = 0;

    void StartPress(uint8_t s, uint32_t now) {
        state = s;
        pressStart = lastHighMs = now;
        lowPending = false;
        highCount = 1;
        dropSum = 0;
    }

    // Frame alto dentro de un press: si venía de un bajón corto, lo puentea.
    // true = se acabó el presupuesto de bajones.
    bool Bridge(uint32_t now) {
        if (highCount < 255) highCount++;
        lastHighMs = now;
        if (!lowPending) return false;
        lowPending = false;
        dropSum += now - lowStart;
        return dropSum > DROP_BUDGET_MS;
    }

    // Frame bajo dentro de un press. true = soltada confirmada (>= RELEASE_MS
    // y >= MIN_SAMPLES); lowStart queda en el 1er frame bajo.
    bool Released(uint32_t now) {
        if (!lowPending) { lowPending = true; lowStart = now; lowCount = 0; }
        if (lowCount < 255) lowCount++;
        if (now - lowStart < RELEASE_MS || lowCount < MIN_SAMPLES) return false;
        lowPending = false;
        return true;
    }

    // Desarmado: cada frame alto empuja el silencio. Además sigue el contacto
    // (mismo criterio de soltada) pa el bloqueo largo, y avisa UNA vez por
    // desarme si llega un contacto nuevo con pinta de tap (no ruido de 1-4
    // frames): así un gesto perdido deja rastro en el serial sin spamearlo.
    Event Disarmed(bool booped, uint32_t now) {
        if (!booped) {
            if (inContact && Released(now)) inContact = false;
            return NONE;
        }
        if (!inContact) {
            inContact = true;
            firedRun = false;
            pressStart = now;
            highCount = 0;
            quietPrev = now - quietRef;
        }
        lowPending = false;
        quietRef = now;
        if (now - pressStart >= (firedRun ? LONG_HOLD_MS : LONG_PRESS_MS)) longRun = true;
        if (warned || highCount == 255) return NONE;  // 255 = la mano que ya venía del cancel/FIRE
        if (++highCount < MIN_SAMPLES || now - pressStart < TAP_MIN_MS) return NONE;
        warned = true;
        return Emit(longRun ? IGNORED_LOCK : IGNORED, quietPrev, 0);
    }

    Event Emit(Event e, uint32_t dur, uint32_t drop) { durMs = dur; dropMs = drop; return e; }

    // Todo cancel/FIRE cae aquí: desarma y cuenta el silencio desde quietFrom.
    // contact = la mano sigue puesta: es el mismo contacto (pressStart se queda
    // pa el bloqueo largo) y ya no se avisa como "ignorado".
    void Quiet(Event why, uint32_t quietFrom, bool contact) {
        state = WAIT_QUIET;
        cause = why;
        quietRef = quietFrom;
        lowPending = false;
        inContact = contact;
        firedRun = false;
        highCount = 255;
        warned = false;
    }

    Event Stop(Event e, uint32_t dur, uint32_t quietFrom, bool contact) {
        uint32_t drop = dropSum;
        Quiet(e, quietFrom, contact);
        firedRun = e == FIRE;
        return Emit(e, dur, drop);
    }
};
