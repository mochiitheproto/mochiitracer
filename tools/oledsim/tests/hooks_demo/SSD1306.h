// SIMULATOR SELF-TEST (not a design proposal): exercises SetBooped/SetFPS hooks,
// a GFX FreeSans font, an extra .cpp, no invertDisplay, dim() and tempmonGetTemp().
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include "../../Examples/UserConfiguration.h"
#include "../InputDevices/Menu/Menu.h"
#include "../../Utils/Math/Mathematics.h"
#include "../../Scene/Screenspace/Effect.h"
#include "../../Utils/Time/TimeStep.h"
#include <Adafruit_SSD1306.h>

extern const uint8_t thermoIcon[];

class HeadsUpDisplay : public Effect {
private:
    Effect* subEffect = nullptr;
    TimeStep timeStep = TimeStep(5);
    bool didBegin = false;
    bool booped = false;
    float fps = 0;
    Vector2D faceMin, faceMax;
    const __FlashStringHelper** faceNames = nullptr;
    static Adafruit_SSD1306 display;
public:
    HeadsUpDisplay(Vector2D faceMin, Vector2D faceMax) : faceMin(faceMin), faceMax(faceMax) {}
    void SetFaceArray(const __FlashStringHelper** f) { faceNames = f; }
    void SetFaceMin(Vector2D v) { faceMin = v; }
    void SetFaceMax(Vector2D v) { faceMax = v; }
    void SetBooped(bool b) { booped = b; }
    void SetFPS(float f) { fps = f; }
    void Initialize();
    void Update();
    void SetEffect(Effect* e) { subEffect = e; }
    void ApplyEffect(IPixelGroup* p) { subEffect->ApplyEffect(p); }
};
