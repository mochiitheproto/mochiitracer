// SIMULATOR SELF-TEST (not a design proposal): exercises the SetBoopGesture hook.
// Latches gesture events between 5 Hz draws, shows a hold bar, and refreshes every
// 100 ms while a gesture is active (TAP1 .. 1.5 s after FIRE/cancel), 5 Hz otherwise.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "../../Examples/UserConfiguration.h"
#include "../InputDevices/Menu/Menu.h"
#include "../../Utils/Math/Mathematics.h"
#include "../../Scene/Screenspace/Effect.h"
#include "../../Utils/Time/TimeStep.h"
#include "../../Examples/Protogen/BoopGesture.h"
#include <Adafruit_SSD1306.h>

class HeadsUpDisplay : public Effect {
private:
    Effect* subEffect = nullptr;
    bool didBegin = false;
    uint32_t lastDraw = 0;
    uint8_t stage = 0;
    uint16_t holdMs = 0;
    uint8_t lastEvent = 0;      // latched: last event that is worth showing
    uint32_t lastEventAt = 0;
    Vector2D faceMin, faceMax;
    const __FlashStringHelper** faceNames = nullptr;
    static Adafruit_SSD1306 display;
public:
    HeadsUpDisplay(Vector2D faceMin, Vector2D faceMax) : faceMin(faceMin), faceMax(faceMax) {}
    void SetFaceArray(const __FlashStringHelper** f) { faceNames = f; }
    void SetFaceMin(Vector2D v) { faceMin = v; }
    void SetFaceMax(Vector2D v) { faceMax = v; }
    void SetBoopGesture(uint8_t stage, uint16_t holdMs, uint8_t event);
    void Initialize();
    void Update();
    void SetEffect(Effect* e) { subEffect = e; }
    void ApplyEffect(IPixelGroup* p) { subEffect->ApplyEffect(p); }
};
