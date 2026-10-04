// OLED SIMULATOR stand-in for ProtoTracer's Menu (same public static API and
// MenuState enum as lib/ProtoTracer/ExternalDevices/InputDevices/Menu/Menu.h).
// Values are plain storage the simulator driver sets per scenario state.
// GetEffect() returns a no-op effect. Only the HUD is simulated, so Update(),
// GetMaterial(), etc. do nothing.
#pragma once

#include "../../../Physics/Utils/DampedSpring.h"
#include "../../../Scene/Screenspace/Effect.h"
#include "../../../Examples/UserConfiguration.h"

class Material;

class Menu {
public:
    enum MenuState {
        Faces,
        Bright,
        AccentBright,
        Microphone,
        MicLevel,
        BoopSensor,
        SpectrumMirror,
        FaceSize,
        Color,
        HueF,
        HueB,
        EffectS,
        FanSpeed
    };

private:
    class NoEffect : public Effect {
    public:
        void ApplyEffect(IPixelGroup *) override {}
    };
    static const uint8_t menuCount = 13;
    static inline uint8_t faceCount = 20;
    static inline uint8_t currentMenu = 0;
    static inline uint8_t values[menuCount] = {0, 3, 5, 1, 5, 1, 1, 7, 0, 0, 0, 0, 0};
    static inline Vector2D position;
    static inline float rotation = 0.0f;
    static NoEffect &noEffect() { static NoEffect e; return e; }

public:
    // Real maxima (Menu::SetMaxEntries): Faces=faceCount, on/off menus 2, rest 10.
    static uint8_t SimMax(uint8_t menu) {
        if (menu == Faces) return faceCount;
        if (menu == Microphone || menu == BoopSensor || menu == SpectrumMirror) return 2;
        return 10;
    }
    static void SimSetFaceCount(uint8_t n) { faceCount = n; }

    static void Initialize(uint8_t faceCount, uint8_t, uint16_t, Vector2D = Vector2D(240, 50)) { Menu::faceCount = faceCount; }
    static void Initialize(uint8_t faceCount, Vector2D = Vector2D(240, 50)) { Menu::faceCount = faceCount; }

    static Material *GetMaterial() { return nullptr; }
    static Effect *GetEffect() { return &noEffect(); }

    static uint8_t GetCurrentMenu() { return currentMenu; }
    static uint8_t GetCurrentMenuValue() { return values[currentMenu < menuCount ? currentMenu : 0]; }
    static void SetCurrentMenu(uint8_t m) { currentMenu = m; }

    static void Update(float) {}
    static void SetWiggleRatio(float) {}
    static void SetWiggleSpeed(float, float, float) {}
    static void SetSize(Vector2D) {}
    static Vector2D GetPosition() { return position; }
    static void SetPosition(Vector2D p) { position = p; }
    static void SetPositionOffset(Vector2D) {}
    static void SetRotationOffset(Vector2D) {}
    static float GetRotation() { return rotation; }
    static void SetRotation(float r) { rotation = r; }

    static void SetFaceState(uint8_t v) { values[Faces] = v; }
    static uint8_t GetFaceState() { return values[Faces]; }
    static void NextFace() { values[Faces] = (values[Faces] + 1) % faceCount; }
    static void PrevFace() { values[Faces] = values[Faces] ? values[Faces] - 1 : faceCount - 1; }

    static bool SetMenuValue(uint8_t menu, uint8_t value) {
        if (menu >= menuCount) return false;
        values[menu] = value;
        return true;
    }
    static void SetBrightness(uint8_t v) { values[Bright] = v; }
    static uint8_t GetBrightness() { return values[Bright]; }
    static void SetAccentBrightness(uint8_t v) { values[AccentBright] = v; }
    static uint8_t GetAccentBrightness() { return values[AccentBright]; }
    static void SetUseMicrophone(uint8_t v) { values[Microphone] = v; }
    static uint8_t UseMicrophone() { return values[Microphone]; }
    static void SetMicLevel(uint8_t v) { values[MicLevel] = v; }
    static uint8_t GetMicLevel() { return values[MicLevel]; }
    static void SetUseBoopSensor(uint8_t v) { values[BoopSensor] = v; }
    static uint8_t UseBoopSensor() { return values[BoopSensor]; }
    static void SetMirrorSpectrumAnalyzer(uint8_t v) { values[SpectrumMirror] = v; }
    static uint8_t MirrorSpectrumAnalyzer() { return values[SpectrumMirror]; }
    static void SetFaceSize(uint8_t v) { values[FaceSize] = v; }
    static uint8_t GetFaceSize() { return values[FaceSize]; }
    static void SetFaceColor(uint8_t v) { values[Color] = v; }
    static uint8_t GetFaceColor() { return values[Color]; }
    static void SetHueF(uint8_t v) { values[HueF] = v; }
    static uint8_t GetHueF() { return values[HueF]; }
    static void SetHueB(uint8_t v) { values[HueB] = v; }
    static uint8_t GetHueB() { return values[HueB]; }
    static void SetEffectS(uint8_t v) { values[EffectS] = v; }
    static uint8_t GetEffectS() { return values[EffectS]; }
    static void SetFanSpeed(uint8_t v) { values[FanSpeed] = v; }
    static uint8_t GetFanSpeed() { return values[FanSpeed]; }
    static float ShowMenu() { return currentMenu != 0 ? 0.0f : 1.0f; }
};
