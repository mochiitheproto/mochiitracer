#include "SSD1306.h"
#include "../../Assets/Screens/Especie.h"  // ESPECIE_NOMBRE: PROTOGEN, or PRIMAGEN with -D ESPECIE_PRIMAGEN

#ifdef SH1106
Adafruit_SH1106 HeadsUpDisplay::display(OLED_RESET);
#else
Adafruit_SSD1306 HeadsUpDisplay::display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

uint8_t HeadsUpDisplay::faceBits[];

namespace {

// ---- what each face looks like on the OLED ---------------------------------------------------
// Matched by the project's faceArray NAME, so reordering or adding faces never mislabels one.
// The name under the visor comes from softFaceTexts (tools/oled/textos.py), in the language.
// pict:  image/video slots show this pictogram instead of their (noisy) silhouette.
// blank: live faces that can render nothing (AUDIO* in silence) show this instead of "...".
struct FaceLook {
    const char* name;
    const SoftIcon* pict;
    const SoftIcon* blank;
};

const FaceLook faceLooks[] = {
    {"BSOD", &pict_bsod, nullptr},
    {"LOWBAT", &pict_lowbat, nullptr},
    {"AUDIO1", nullptr, &pict_rings},
    {"AUDIO2", nullptr, &pict_eq},
    {"KAOMOJI", &pict_kaomoji, nullptr},   // cycles ^w^ / <3w<3 / >w< like the LED strobe
    {"KP140", &pict_heart, nullptr},
};

// ---- what each setting looks like while it is being edited ----------------------------------
enum SettingKind : uint8_t { LEVEL, TOGGLE, LIST };

struct SettingLook {
    const SoftIcon* icon;
    SettingKind kind;
};

// Index = Menu::MenuState (0 = Faces is normal use, never shown here). The words (label and the
// colour / hue / effect names) are SoftAssets' softSettingWords, softColorWords, softHueWords and
// softEffectWords, in the language.
const SettingLook settingLooks[13] = {
    {nullptr, LEVEL},
    {&icon_sun, LEVEL},        // Bright
    {&icon_sides, LEVEL},      // AccentBright
    {&icon_mic, TOGGLE},       // Microphone
    {&icon_ear, LEVEL},        // MicLevel
    {&icon_boop, TOGGLE},      // BoopSensor
    {&icon_mirror_on, TOGGLE}, // SpectrumMirror
    {&icon_size, LEVEL},       // FaceSize
    {&icon_drop, LIST},        // Color
    {&icon_hue, LIST},         // HueF
    {&icon_hue, LIST},         // HueB
    {&icon_wand, LIST},        // EffectS
    {&icon_fan0, LEVEL},       // FanSpeed
};

// Burn-in care: the idle picture walks around a 3x3 square, one step per minute.
const int8_t driftX[8] = {0, 1, 1, 0, -1, -1, -1, 0};
const int8_t driftY[8] = {0, 0, 1, 1, 1, 0, -1, -1};

}  // namespace

HeadsUpDisplay::HeadsUpDisplay(Vector2D faceMin, Vector2D faceMax) {
    this->faceMin = faceMin;
    this->faceMax = faceMax;
}

FLASHMEM void HeadsUpDisplay::SetFaceArray(const __FlashStringHelper** faceNames, uint8_t count) {
    this->faceNames = faceNames;
    this->faceCount = count ? count : defaultFaceSlots;
    useExternalFace = true;
    lastFace = -1;  // re-pick the label/pictogram
}

void HeadsUpDisplay::SetFaceMin(Vector2D faceMin) {
    this->faceMin = faceMin;
}

void HeadsUpDisplay::SetFaceMax(Vector2D faceMax) {
    this->faceMax = faceMax;
}

FLASHMEM void HeadsUpDisplay::SetLanguage(uint8_t language) {
    if (language >= SOFT_LANGS) language = IDIOMA_DEFECTO;
    if (language == lang) return;
    lang = language;
    lastFace = -1;  // the face's name again, in the new language (no wake, no sparkles)
}

FLASHMEM uint8_t HeadsUpDisplay::GetLanguage() const {
    return lang;
}

FLASHMEM void HeadsUpDisplay::SetTempOverride(float celsius) {
    tempOverride = celsius > 0.0f ? celsius : 0.0f;
}

FLASHMEM float HeadsUpDisplay::GetTempOverride() const {
    return tempOverride;
}

FLASHMEM const uint8_t* HeadsUpDisplay::GetBuffer() const {
#ifdef SH1106
    return nullptr;
#else
    return didBegin ? display.getBuffer() : nullptr;
#endif
}

FLASHMEM bool HeadsUpDisplay::IsInverted() const {
    return false;  // normal polarity since the redesign: buffer bit 1 = lit pixel
}

FLASHMEM uint8_t HeadsUpDisplay::GetContrast() const {
    return contrastNow;
}

FLASHMEM void HeadsUpDisplay::Initialize() {
    ResetDisplayBuffer();

    Wire.setClock(100000);//for longer range transmissions
    Wire.begin();

    #ifdef WS35
    Wire.setSDA(19);
    Wire.setSCL(18);
    #else
    Wire.setSDA(18);
    Wire.setSCL(19);
    #endif

    Wire.beginTransmission(0x3C);
    uint8_t error = Wire.endTransmission();

    if(error == 0){// SSD1306 Found
        #ifdef SH1106
        display.begin(SH1106_SWITCHCAPVCC, 0x3C);
        didBegin = true;
        #else
        didBegin = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
        #endif

        if (didBegin) {
            // Normal polarity: buffer bit 1 = lit pixel. Start black (no white flash at boot).
            display.clearDisplay();
            display.display();
            SetContrast(contrastCalm);
        }
    }
    else {
        didBegin = false;
    }

    startMillis = millis();
}

FLASHMEM void HeadsUpDisplay::ResetI2CBus() {
    Wire.end();   // Disable the I2C hardware
    delay(10);    // Wait for the I2C bus to settle
    Wire.begin(); // Enable the I2C hardware
}

FLASHMEM void HeadsUpDisplay::Update() {
    if (timeStep.IsReady()) framePending = true;  // the usual 5 Hz beat
    if (!didBegin) return;

    // A boop gesture adds frames between beats when its picture changes, and while one is on
    // screen no two frames go out closer than boopMinGap (a beat waits a few frames for it).
    // Without a gesture only beats draw and they are >= 200 ms apart: same frames as always.
    // While the 1st boop is pressed (<= TAP_MAX_MS) the beat waits too, so the bus is free when
    // TAP1 arrives (its frame takes the beat along). Not when very hot: that light keeps its pace.
    uint32_t now = millis();
    uint8_t look = splashFinished ? BoopLook(now) : LOOK_NONE;
    bool pressQuiet = boopStage == BoopGesture::STAGE_TAP && warnLevel < 2;
    if ((!framePending || pressQuiet) && look == drawnLook) return;
    if (sentOnce && now - lastSentAt < boopMinGap) return;
    bool beat = framePending;
    framePending = false;
    boopLook = drawnLook = look;

    if (beat) {
        updateCount++;
        UpdateWarnLevel();
    }

    uint32_t t = millis() - startMillis;
    display.clearDisplay();

    if (splashFinished) {
        UpdateFaceInformation();
    } else if (t < creditTime) {
        SetContrast(contrastCalm);
        DrawCredit();
    } else if (t < creditTime + bootTime) {
        SetContrast(contrastCalm);
        DrawBoot(t - creditTime);
    } else {
        splashFinished = true;
        wakeAt = millis();  // "lights on" when the face first shows
        UpdateFaceInformation();
    }

    SendFrame(beat);
}

FLASHMEM void HeadsUpDisplay::UpdateFaceInformation() {
    uint32_t now = millis();
    uint8_t menu = Menu::GetCurrentMenu();
    bool editing = menu >= 1 && menu <= 12;

    if (wasEditing && !editing) wakeAt = now;  // back to the face: show it bright for a moment
    wasEditing = editing;

    bool warnOn = false;
    if (warnLevel == 1) warnOn = (warnTick % 8) < 5;  // 1 s on, 0.6 s off: gentle
    else if (warnLevel == 2) warnOn = !(warnTick & 1); // 2.5 Hz: urgent

    if (editing) {
        DrawEditScreen(menu);
        if (warnOn) DrawCornerLight();
        if (boopLook) DrawBoopCorner();
        SetContrast(contrastActive);
        litLook = LOOK_NONE;
    } else {
        DrawFaceScreen(now, warnOn);
        // booping lights it up; the 1st boop alone only if boopBrightAtTap
        uint8_t lit = boopLook == 1 && !boopBrightAtTap ? LOOK_NONE : boopLook;
        // the line went away without a face change (single boop, let go): back to calm through
        // the usual 3-step fade instead of snapping dark (a FIRE's new name has its own wake)
        // (signed: right after a FIRE wakeAt points at the end of the check bar, in the future)
        if (litLook && !lit && litLook != LOOK_DONE && (int32_t)(now - wakeAt) >= (int32_t)wakeTime) wakeAt = now - wakeTime;
        litLook = lit;
        SetContrast(lit ? contrastActive : IdleContrast(now, warnOn));
    }
}

FLASHMEM uint8_t HeadsUpDisplay::IdleContrast(uint32_t now, bool warnOn) const {
    if (warnLevel == 2 || (warnLevel == 1 && warnOn)) return contrastActive;  // the light flashes the panel

    uint32_t since = now - wakeAt;
    if (since < wakeTime) return contrastActive;
    if (since < wakeTime + fadeTime) {  // three steps down to calm
        uint32_t k = (since - wakeTime) * 3 / fadeTime + 1;
        return contrastActive - (contrastActive - contrastCalm) * k / 4;
    }
    return contrastCalm;
}

// Only sends the buffer when it changed (plus a periodic refresh, counted in 5 Hz beats): fewer
// 23 ms I2C transfers competing with the boop sensor on the same bus.
FLASHMEM void HeadsUpDisplay::SendFrame(bool beat) {
#ifndef SH1106
    const uint8_t* buf = display.getBuffer();
    uint32_t hash = 2166136261u;
    for (uint16_t i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT / 8; i++) hash = (hash ^ buf[i]) * 16777619u;

    if (hash == lastHash && (!beat || ++sameFrames < refreshEvery)) return;
    lastHash = hash;
    sameFrames = 0;
#endif

    unsigned long cmdTime = millis();
    lastSentAt = cmdTime;
    sentOnce = true;

    display.display();

    if (millis() - cmdTime > 250) {
        // Timeout occurred
        ResetI2CBus();
        lastHash = 0;  // resend next time
    }
}

FLASHMEM void HeadsUpDisplay::SetContrast(uint8_t contrast) {
    if (contrast == contrastNow) return;
    contrastNow = contrast;
    #ifndef SH1106
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(contrast);
    #endif
}

FLASHMEM void HeadsUpDisplay::UpdateWarnLevel() {
    float c = tempOverride > 0.0f ? tempOverride : tempmonGetTemp();
    uint8_t level = warnLevel;

    if (level == 2 && c < veryHotOffC) level = 1;
    else if (level < 2 && c >= veryHotOnC) level = 2;

    if (level == 1 && c < hotOffC) level = 0;
    else if (level == 0 && c >= hotOnC) level = 1;

    if (level != warnLevel) {
        warnLevel = level;
        warnTick = 0;  // a new state starts with the light on
    } else {
        warnTick++;
    }
}

void HeadsUpDisplay::SetEffect(Effect* effect) {
    subEffect = effect;
}

void HeadsUpDisplay::ApplyEffect(IPixelGroup* pixelGroup) {
    //Actually apply effect
    if (subEffect) subEffect->ApplyEffect(pixelGroup);

    // Only the main camera is the face: the Delta side cameras are much smaller groups.
    uint16_t pixelCount = pixelGroup->GetPixelCount();
    if (pixelCount > largestGroup) largestGroup = pixelCount;
    if (pixelCount < largestGroup) return;

    float spanX = faceMax.X - faceMin.X;
    float spanY = faceMax.Y - faceMin.Y;
    if (spanX <= 0.0f || spanY <= 0.0f) return;

    memset(faceBits, 0, sizeof(faceBits));  // keep only the latest frame (blinks show)

    for (uint16_t i = 0; i < pixelCount; i++) {
        RGBColor* color = pixelGroup->GetColor(i);
        if (!(color->R | color->G | color->B)) continue;  // blue counts too

        Vector2D p = pixelGroup->GetCoordinate(i);
        int col = (int)((p.X - faceMin.X) * srcW / spanX);
        int row = (int)((p.Y - faceMin.Y) * srcH / spanY);
        if (col < 0 || col >= srcW || row < 0 || row >= srcH) continue;

        row = srcH - 1 - row;  // the camera counts y upwards
        faceBits[row * (srcW / 8) + (col >> 3)] |= 0x80 >> (col & 7);
    }
}

FLASHMEM void HeadsUpDisplay::ResetDisplayBuffer() {
    memset(faceBits, 0, sizeof(faceBits));
}

// ---- screens ---------------------------------------------------------------------------------

FLASHMEM void HeadsUpDisplay::DrawCredit() {
    // The original ProtoTracer / AGPLv3 splash (stored inverted: bit 0 = lit).
    display.drawBitmap(0, 0, PrototracerSplash, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 1);
}

FLASHMEM void HeadsUpDisplay::DrawBoot(uint32_t t) {
    // The visor wakes up: sleepy eyes open and blink while the dots load (and get stuck on the
    // last one, like the LED bar), then ^ ^ and "¡HOLA!".
    ox = oy = 0;
    DrawVisor();

    const int16_t eyeY = frameY + 12;
    const int16_t eyeDX = 26;
    const uint32_t done = 2300;

    if (t >= done) {  // happy ^ ^
        for (int8_t s = -1; s <= 1; s += 2) {
            int16_t cx = 64 + s * eyeDX;
            for (int8_t k = 0; k < 2; k++) {
                display.drawLine(cx - 7, eyeY + 3 + k, cx, eyeY - 4 + k, 1);
                display.drawLine(cx, eyeY - 4 + k, cx + 7, eyeY + 3 + k, 1);
            }
        }
    } else {
        // closed -> opening -> open, with one sleepy blink
        int16_t h = 2;
        if (t > 400) h = 2 + (t - 400) / 60;
        if (h > 10) h = 10;
        if (t > 1500 && t < 1650) h = 2;
        for (int8_t s = -1; s <= 1; s += 2) {
            int16_t cx = 64 + s * eyeDX;
            int16_t y0 = eyeY - h / 2;
            for (int16_t j = 0; j < h; j++) {  // soft rounded blob, row by row
                int16_t edge = j < h - 1 - j ? j : h - 1 - j;
                int16_t in = h >= 6 ? (edge == 0 ? 3 : (edge == 1 ? 1 : 0)) : (h >= 3 && edge == 0 ? 1 : 0);
                display.drawFastHLine(cx - 7 + in, y0 + j, 14 - 2 * in, 1);
            }
        }
    }

    DrawIcon(wid_mouth_w, 56, frameY + 20);  // little w mouth

    if (t >= done) {
        DrawTextCentered(softWords[lang][WORD_HELLO], 64, nameY, softFontL);
        return;
    }

    DrawTextCentered(ESPECIE_NOMBRE " OS", 64, frameY + frameH + 5, softFontS, true);

    // loading dots: quick, then it gets stuck on the last one (like every loading bar)
    uint8_t filled;
    if (t < 300) filled = 0;
    else if (t < 1500) filled = 1 + (t - 300) * 8 / 1200;     // 1..9
    else filled = 9;
    if (filled > 9) filled = 9;
    if (t >= 1500 && (updateCount & 1)) filled = 10;  // the last dot blinks while "stuck"
    DrawDots(frameY + frameH + 17, filled, 10);
}

FLASHMEM void HeadsUpDisplay::DrawVisor() {
    // rounded 2 px visor with two little ears on top, so it reads as a protogen head
    DrawIcon(wid_visor, frameX + ox, visorTop + oy);
}

FLASHMEM const char* HeadsUpDisplay::FaceName(uint8_t index) const {
    const char* name = nullptr;
    if (useExternalFace && faceNames) {
        if (index < faceCount) name = reinterpret_cast<const char*>(faceNames[index]);
    } else if (index < 10) {
        name = defaultFaceNames[index];
    }
    return name;
}

FLASHMEM void HeadsUpDisplay::PickFaceLooks(uint8_t index) {
    const char* name = FaceName(index);
    faceLabel = name ? name : softWords[lang][WORD_FACE];  // out of range (e.g. an old EEPROM value): generic word
    facePict = blankPict = nullptr;
    nameDeco = nullptr;
    if (!name) return;

    for (const FaceLook& f : faceLooks) {
        if (strcmp(name, f.name) == 0) {
            facePict = f.pict;
            blankPict = f.blank;
            break;
        }
    }
    for (uint8_t i = 0; i < SOFT_FACE_TEXTS; i++) {  // its name in the language (none: the name as is)
        if (strcmp(name, softFaceTexts[i].name) == 0) {
            if (softFaceTexts[i].label[lang]) faceLabel = softFaceTexts[i].label[lang];
            break;
        }
    }
    if (strcmp(name, "AMOR") == 0) nameDeco = &heart_big;
}

FLASHMEM void HeadsUpDisplay::DrawFaceScreen(uint32_t now, bool warnOn) {
    uint8_t face = Menu::GetFaceState();

    if (face != lastFace) {
        if (lastFace >= 0) {
            faceChangedAt = wakeAt = now;
            // changed by the boop: the check bar first, then the new name wakes up with its sparkles
            if (boopLook == LOOK_DONE) faceChangedAt = wakeAt = boopAt + boopDoneTime;
        }
        lastFace = face;
        PickFaceLooks(face);
    }

    uint8_t step = (now / driftTime) % 8;
    ox = driftX[step];
    oy = driftY[step];

    DrawVisor();

    if (warnLevel == 2 && warnOn) {
        // very hot: the whole visor lights up with the light cut out of it
        int16_t x = frameX + ox, y = frameY + oy;
        display.fillRect(x + 4, y + 2, frameW - 8, frameH - 4, 1);
        display.fillRect(x + 2, y + 4, frameW - 4, frameH - 8, 1);
        DrawIcon(icon_coolant_big, 64 - icon_coolant_big.w / 2 + ox, y + (frameH - icon_coolant_big.h) / 2, 0);
    } else {
        DrawMiniFace(now);
        if (warnLevel == 1 && warnOn) {
            // hot: the "engine hot" light pops up between the eyes, over the top of the visor
            int16_t x = 64 - wid_tile.w / 2 + ox, y = visorTop - 2 + oy;
            DrawIcon(wid_tile, x, y, 0);
            DrawIcon(icon_coolant, x + 2, y + 2);
        }
    }

    if (boopLook) DrawBoopLine();  // the name line is the boop's status line for a moment
    else DrawFaceName(now);
}

FLASHMEM void HeadsUpDisplay::DrawFaceName(uint32_t now) {
    const SoftFont* font = &softFontL;
    bool bold = false;
    int16_t w = TextWidth(faceLabel, softFontL);
    if (w > SCREEN_WIDTH - 6) {  // very long custom names fall back to the small font
        font = &softFontS;
        bold = true;
        w = TextWidth(faceLabel, softFontS, true);
    }
    int16_t x = 64 - w / 2 + ox;
    int16_t y = nameY + (14 - font->capHeight) / 2 + oy;
    DrawText(faceLabel, x, y, *font, bold);

    // right after a face change: sparkles (hearts for AMOR) on both sides, if there is room
    uint32_t since = now - faceChangedAt;
    if (faceChangedAt == 0 || since >= sparkleTime) return;
    const SoftIcon* s;
    if (nameDeco) s = since < 400 || since >= 800 ? nameDeco : &heart_small;
    else s = since < 400 ? &spark_big : (since < 800 ? &spark_small : &spark_dot);
    int16_t cy = nameY + 7 + oy;
    int16_t left = x - 5 - s->w, right = x + w + 4;
    if (left < 2 || right + s->w > SCREEN_WIDTH - 2) return;
    DrawIcon(*s, left, cy - s->h / 2);
    DrawIcon(*s, right, cy - s->h / 2);
}

FLASHMEM void HeadsUpDisplay::DrawMiniFace(uint32_t now) {
    const int16_t areaCY = frameY + frameH / 2 + oy;

    if (facePict) {
        const SoftIcon* p = facePict;
        if (p == &pict_kaomoji) {  // the KAOMOJI slot: a different little face every second
            static const SoftIcon* const kaomojis[3] = {&pict_kaomoji, &pict_kaomoji_love, &pict_kaomoji_x};
            p = kaomojis[(now / 1000) % 3];
        }
        DrawIcon(*p, 64 - p->w / 2 + ox, areaCY - p->h / 2);
        return;
    }

    uint16_t lit = 0;
    for (uint16_t i = 0; i < sizeof(faceBits); i++) {
        uint8_t b = faceBits[i];
        while (b) { lit += b & 1; b >>= 1; }
    }

    if (lit == 0) {  // nothing rendered: the face's own icon (AUDIO*), else three soft dots
        if (blankPict) DrawIcon(*blankPict, 64 - blankPict->w / 2 + ox, areaCY - blankPict->h / 2);
        else for (int8_t k = -1; k <= 1; k++) display.fillRect(63 + k * 6 + ox, areaCY, 2, 2, 1);
        return;
    }
    if (lit > srcW * srcH * 45 / 100) {  // a full-screen picture: show a "video" pictogram
        DrawIcon(pict_play, 64 - pict_play.w / 2 + ox, areaCY - pict_play.h / 2);
        return;
    }

    // [mirror | normal], like people see the two panels from the front
    for (uint8_t r = 0; r < srcH; r++) {
        int16_t y = faceY + r * 4 / 5 + oy;
        for (uint8_t c = 0; c < srcW; c++) {
            if (!(faceBits[r * (srcW / 8) + (c >> 3)] & (0x80 >> (c & 7)))) continue;
            int16_t t = c * 4 / 5;
            display.drawPixel(faceX + halfW + t + ox, y, 1);
            display.drawPixel(faceX + halfW - 1 - t + ox, y, 1);
        }
    }
}

FLASHMEM void HeadsUpDisplay::DrawEditScreen(uint8_t menu) {
    const SettingLook& look = settingLooks[menu];
    const int16_t iconY = 0, labelY = 34, wordY = 49, valueCY = 55;  // accents of the big word stay clear of the label

    uint8_t v = 0;
    const char* word = nullptr;
    switch (menu) {
        case Menu::Bright:         v = Menu::GetBrightness(); break;
        case Menu::AccentBright:   v = Menu::GetAccentBrightness(); break;
        case Menu::Microphone:     v = Menu::UseMicrophone(); break;
        case Menu::MicLevel:       v = Menu::GetMicLevel(); break;
        case Menu::BoopSensor:     v = Menu::UseBoopSensor(); break;
        case Menu::SpectrumMirror: v = Menu::MirrorSpectrumAnalyzer(); break;
        case Menu::FaceSize:       v = Menu::GetFaceSize(); break;
        case Menu::Color:          v = Menu::GetFaceColor(); word = softColorWords[lang][v % 10]; break;
        case Menu::HueF:           v = Menu::GetHueF(); word = softHueWords[lang][v % 10]; break;
        case Menu::HueB:           v = Menu::GetHueB(); word = softHueWords[lang][v % 10]; break;
        case Menu::EffectS:        v = Menu::GetEffectS(); word = softEffectWords[lang][v % 10]; break;
        case Menu::FanSpeed:       v = Menu::GetFanSpeed(); break;
    }

    const SoftIcon* icon = look.icon;
    bool slashed = false;
    switch (menu) {
        case Menu::Microphone:
        case Menu::BoopSensor:     slashed = !v; break;
        case Menu::SpectrumMirror: if (!v) icon = &icon_mirror_off; break;
        case Menu::Color: {
            uint8_t c = v % 10;
            icon = c == 0 ? &icon_drop_grad : (c == 8 ? &icon_drop_bands : (c == 9 ? &icon_drop_noise : &icon_drop_solid));
            break;
        }
        case Menu::EffectS:        slashed = (v % 10) == 0; break;
        case Menu::FanSpeed: {     // stopped and slashed at 0, spins faster with the level
            static const SoftIcon* const fanFrames[3] = {&icon_fan0, &icon_fan1, &icon_fan2};
            uint8_t lv = v > 9 ? 9 : v;
            icon = fanFrames[(updateCount * lv / 9) % 3];
            slashed = !v;
            break;
        }
    }
    int16_t ix = 64 - icon->w / 2;
    DrawIcon(*icon, ix, iconY);
    if (slashed) DrawSlash(ix, iconY, icon->w);

    DrawTextCentered(softSettingWords[lang][menu], 64, labelY, softFontS, true);

    if (look.kind == LEVEL) {
        if (menu == Menu::FanSpeed) DrawDots(valueCY, v, 9);  // 0 = fans off, 9 = full
        else DrawDots(valueCY, v + 1, 10);                     // 0 = lowest, still on
    } else if (look.kind == TOGGLE) {
        DrawToggle(valueCY, v);
    } else if (word) {
        if (TextWidth(word, softFontL) <= SCREEN_WIDTH - 6) DrawTextCentered(word, 64, wordY, softFontL);
        else DrawTextCentered(word, 64, valueCY - 3, softFontS, true);
    }
}

FLASHMEM void HeadsUpDisplay::DrawCornerLight() {
    // editing while hot: the light blinks in the top-left corner, clear of the setting's icon
    if (warnLevel == 2) {  // very hot: lit tile with the light cut out
        DrawIcon(wid_tile, 1, 1, 1);
        DrawIcon(icon_coolant, 3, 3, 0);
    } else {
        DrawIcon(icon_coolant, 3, 3, 1);
    }
}

// ---- boop gesture ----------------------------------------------------------------------------

HUD_COLD void HeadsUpDisplay::SetBoopGesture(uint8_t stage, uint16_t holdMs, uint8_t event) {
    boopStage = stage;
    boopHoldMs = holdMs;
    switch (event) {
        case BoopGesture::TAP1:
        case BoopGesture::HOLD_START:
            boopMode = BOOP_TRACK;
            break;
        case BoopGesture::FIRE:
            boopMode = BOOP_DONE;
            boopAt = millis();
            break;
        case BoopGesture::CANCEL_TAP2_SHORT:   // let go before the hold (or a normal double tap)
        case BoopGesture::CANCEL_HOLD_BROKEN:
        case BoopGesture::CANCEL_HOLD_FEW:
        case BoopGesture::CANCEL_STALL:
            if (boopMode == BOOP_TRACK) {
                boopMode = BOOP_FIZZLE;
                boopAt = millis();
            }
            break;
        case BoopGesture::CANCEL_GAP_TIMEOUT:  // a plain single boop (strangers at a con): just goes away
            if (boopMode == BOOP_TRACK) boopMode = BOOP_OFF;
            break;
        default:  // NONE, noise, a 1st press that was no tap (hug), IGNORED*: nothing on screen
            break;
    }
    if (boopMode == BOOP_TRACK && stage < BoopGesture::STAGE_TAP) boopMode = BOOP_OFF;  // Reset(): boot, sensor off
    if (boopMode >= BOOP_DONE && millis() - boopAt >= boopCornerTime) boopMode = BOOP_OFF;  // the longest one is over
}

// What the boop layer shows now (also tells Update() when to send an extra frame).
HUD_COLD uint8_t HeadsUpDisplay::BoopLook(uint32_t now) const {
    uint8_t menu = Menu::GetCurrentMenu();
    bool editing = menu >= 1 && menu <= 12;
    uint32_t since = now - boopAt;

    switch (boopMode) {
        case BOOP_TRACK: {
            if (editing) return LOOK_CORNER;
            if (boopStage != BoopGesture::STAGE_HOLD) return 1;  // 1st boop caught
            if (boopHoldMs < boopDot3At) return 2;               // 2nd boop caught (or a plain double tap)
            if (boopHoldMs < boopDot4At) return 3;
            return boopHoldMs < boopDot5At ? 4 : boopDots;       // 5 just before it fires
        }
        case BOOP_DONE:
            if (editing) return since < boopCornerTime ? LOOK_CORNER_DONE : LOOK_NONE;
            return since < boopDoneTime ? LOOK_DONE : LOOK_NONE;
        case BOOP_FIZZLE:
            return !editing && since < boopFizzleTime ? LOOK_FIZZLE : LOOK_NONE;
        default:
            return LOOK_NONE;
    }
}

HUD_COLD void HeadsUpDisplay::DrawBoopLine() {
    // [hand] o o o o o under the visor, on the name's centre line (and its burn-in drift)
    int16_t cy = nameY + 7 + oy;
    int16_t x = boopLineX + ox;
    DrawIcon(boop_hand, x, cy - boop_hand.h / 2);
    x += boop_hand.w + 8;

    if (boopLook == LOOK_DONE) {  // the dots fuse into one lit bar with a check cut out
        int16_t bx = x - 2;
        DrawIcon(wid_capsule, bx, cy - wid_capsule.h / 2);
        DrawIcon(icon_check, bx + (wid_capsule.w - icon_check.w) / 2, cy - icon_check.h / 2, 0);
        return;
    }
    uint8_t lit = boopLook <= boopDots ? boopLook : 0;  // LOOK_FIZZLE: all empty
    for (uint8_t i = 0; i < boopDots; i++) {
        DrawIcon(i < lit ? wid_pip_on : wid_pip_off, x + i * boopPitch, cy - wid_pip_on.h / 2);
    }
}

HUD_COLD void HeadsUpDisplay::DrawBoopCorner() {
    // edit screens: top right, mirroring the warning light's corner; the face changes under the menu
    const int16_t x = SCREEN_WIDTH - 1 - wid_tile.w, y = 1;
    if (boopLook == LOOK_CORNER_DONE) {
        DrawIcon(wid_tile, x, y, 1);
        DrawIcon(icon_check, x + (wid_tile.w - icon_check.w) / 2, y + (wid_tile.h - icon_check.h) / 2, 0);
    } else if (boopLook == LOOK_CORNER) {
        DrawIcon(boop_hand, x + (wid_tile.w - boop_hand.w) / 2, y + (wid_tile.h - boop_hand.h) / 2);
    }
}

// ---- widgets ---------------------------------------------------------------------------------

FLASHMEM void HeadsUpDisplay::DrawDots(int16_t cy, uint8_t filled, uint8_t total) {
    const int16_t pitch = 11, dot = 7;
    int16_t x0 = (SCREEN_WIDTH - ((total - 1) * pitch + dot)) / 2;  // same margin left and right
    for (uint8_t i = 0; i < total; i++) {
        DrawIcon(i < filled ? wid_dot_on : wid_dot_off, x0 + i * pitch, cy - dot / 2);
    }
}

FLASHMEM void HeadsUpDisplay::DrawToggle(int16_t cy, bool on) {
    // NO (pill) SI: the pill shows the state, the active word is big (Chinese: bold, same size)
    const SoftIcon& pill = on ? wid_toggle_on : wid_toggle_off;
    int16_t px = 64 - pill.w / 2;
    DrawIcon(pill, px, cy - pill.h / 2);

    const char* no = softWords[lang][WORD_OFF];
    const char* yes = softWords[lang][WORD_ON];
    int16_t gap = 6;
    if (on) {
        DrawText(no, px - gap - TextWidth(no, softFontS, true), cy - 3, softFontS, true);
        DrawText(yes, px + pill.w + gap, cy - 6, softFontL, false, 1, true);
    } else {
        DrawText(no, px - gap - TextWidth(no, softFontL, false, true), cy - 6, softFontL, false, 1, true);
        DrawText(yes, px + pill.w + gap, cy - 3, softFontS, true);
    }
}

FLASHMEM void HeadsUpDisplay::DrawSlash(int16_t x, int16_t y, int16_t size) {
    // black halo first, then a 2 px white stroke from top-left to bottom-right
    for (int8_t o = -3; o <= 3; o++) display.drawLine(x + 2 + o, y + 1, x + size - 2 + o, y + size - 3, 0);
    for (int8_t o = 0; o <= 1; o++) display.drawLine(x + 2 + o, y + 1, x + size - 2 + o, y + size - 3, 1);
}

FLASHMEM void HeadsUpDisplay::DrawIcon(const SoftIcon& icon, int16_t x, int16_t y, uint16_t color) {
    display.drawBitmap(x, y, icon.bits, icon.w, icon.h, color);
}

// ---- text ------------------------------------------------------------------------------------

FLASHMEM uint16_t HeadsUpDisplay::NextCode(const char*& s) {
    uint8_t c = (uint8_t)*s++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && *s) {  // 2-byte UTF-8 (accents, Ñ, ¡ ¿)
        uint16_t code = ((c & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
        return code;
    }
    if ((c & 0xF0) == 0xE0 && s[0] && s[1]) {  // 3-byte UTF-8 (Chinese)
        uint16_t code = ((c & 0x0F) << 12) | (((uint8_t)s[0] & 0x3F) << 6) | ((uint8_t)s[1] & 0x3F);
        s += 2;
        return code;
    }
    return '?';
}

FLASHMEM const SoftGlyph* HeadsUpDisplay::FindGlyph(const SoftFont& font, uint16_t code) const {
    if (code >= 'a' && code <= 'z') code -= 32;  // caps-only font
    for (uint8_t i = 0; i < font.count; i++) {
        if (font.glyphs[i].code == code) return &font.glyphs[i];
    }
    for (uint8_t i = 0; i < font.count; i++) {
        if (font.glyphs[i].code == '?') return &font.glyphs[i];
    }
    return nullptr;
}

// Chinese (code >= cjkFirst) from softFontCJK when it has the character; the rest from `font`.
FLASHMEM const SoftGlyph* HeadsUpDisplay::PickGlyph(const SoftFont& font, uint16_t code, const SoftFont*& from) const {
    if (code >= cjkFirst) {
        for (uint8_t i = 0; i < softFontCJK.count; i++) {
            if (softFontCJK.glyphs[i].code == code) {
                from = &softFontCJK;
                return &softFontCJK.glyphs[i];
            }
        }
    }
    from = &font;
    return FindGlyph(font, code);
}

// bold = the small font drawn twice, 1 px apart (sturdier at arm's length). The gap after a glyph
// is its own font's spacing (tools/oled/gen_assets.py measures the strings the same way).
FLASHMEM int16_t HeadsUpDisplay::TextWidth(const char* s, const SoftFont& font, bool bold, bool cjkBold) const {
    int16_t w = 0;
    uint8_t gap = 0;
    bool first = true;
    while (*s) {
        const SoftFont* f;
        const SoftGlyph* g = PickGlyph(font, NextCode(s), f);
        if (!g) continue;
        if (!first) w += gap;
        w += g->w + ((f == &font ? bold : cjkBold) ? 1 : 0);
        gap = f->spacing;
        first = false;
    }
    return w;
}

// A Chinese character is centred on the line of `font`'s caps (12 px font: 1 px lower than the
// big caps' top, 2 px higher than the small caps' top).
FLASHMEM int16_t HeadsUpDisplay::DrawText(const char* s, int16_t x, int16_t y, const SoftFont& font, bool bold, uint16_t color, bool cjkBold) {
    while (*s) {
        const SoftFont* f;
        const SoftGlyph* g = PickGlyph(font, NextCode(s), f);
        if (!g) continue;
        bool b = f == &font ? bold : cjkBold;
        int16_t gy = y + g->top + (f == &font ? 0 : ((int16_t)font.capHeight - (int16_t)f->capHeight) / 2);
        display.drawBitmap(x, gy, f->bits + g->off, g->w, g->h, color);
        if (b) display.drawBitmap(x + 1, gy, f->bits + g->off, g->w, g->h, color);
        x += g->w + (b ? 1 : 0) + f->spacing;
    }
    return x;
}

FLASHMEM int16_t HeadsUpDisplay::DrawTextCentered(const char* s, int16_t cx, int16_t y, const SoftFont& font, bool bold, uint16_t color) {
    int16_t w = TextWidth(s, font, bold);
    DrawText(s, cx - w / 2, y, font, bold, color);
    return w;
}

// Original ProtoTracer splash by Coela Can't (AGPLv3 notice + QR to the source).
PROGMEM const uint8_t HeadsUpDisplay::PrototracerSplash[] = {
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xF8, 0x00, 0xFF, 0xFF, 0xFE, 0x03, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xF0, 0x00, 0x7F, 0xFF, 0xFE, 0x01, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xF1, 0xFC, 0x3F, 0xFF, 0xFE, 0x71, 0xFF, 0xF3, 0x8F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xF1, 0xFC, 0x00, 0x00, 0x00, 0x70, 0x00, 0x03, 0x80, 0x01, 0x00, 0x20, 0x08, 0x00, 0x00, 0x7F,
0xF1, 0xC7, 0x00, 0x00, 0x00, 0x70, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F,
0xF1, 0xC7, 0x7F, 0x87, 0xE3, 0xFE, 0x3F, 0x1F, 0xF3, 0xF8, 0x3F, 0x0F, 0xC1, 0xF8, 0xFE, 0x1F,
0xF1, 0xC7, 0x7F, 0x87, 0xE3, 0xFE, 0x3F, 0x1F, 0xF3, 0xF8, 0x3F, 0x0F, 0xC1, 0xF8, 0xFE, 0x1F,
0xF1, 0xC7, 0x71, 0xCE, 0x38, 0x70, 0x71, 0xC3, 0x83, 0x8E, 0xE3, 0x9C, 0x77, 0x1C, 0xE3, 0x9F,
0xF1, 0xFC, 0x71, 0xCE, 0x38, 0x70, 0x71, 0xC3, 0x83, 0x8E, 0x03, 0x9C, 0x77, 0x1C, 0xE3, 0x9F,
0xF1, 0xFC, 0x71, 0xCE, 0x38, 0x70, 0x71, 0xC3, 0x83, 0x8E, 0x03, 0x9C, 0x77, 0x1C, 0xE3, 0x9F,
0xF1, 0xC0, 0x70, 0x0E, 0x38, 0x70, 0x71, 0xC3, 0x83, 0x80, 0x3F, 0x9C, 0x07, 0xFC, 0xE0, 0x1F,
0xF1, 0xC0, 0x70, 0x0E, 0x38, 0x70, 0x71, 0xC3, 0x83, 0x80, 0xE3, 0x9C, 0x77, 0x00, 0xE0, 0x3F,
0xF1, 0xCE, 0x71, 0x8E, 0x38, 0x70, 0x71, 0xC3, 0x83, 0x9C, 0xE3, 0x9C, 0x77, 0x00, 0xE7, 0xFF,
0xF1, 0xCE, 0x71, 0x8E, 0x38, 0x70, 0x71, 0xC3, 0x83, 0x9C, 0xE3, 0x9C, 0x77, 0x1C, 0xE7, 0xFF,
0xF1, 0xCE, 0x71, 0x87, 0xE0, 0x1E, 0x3F, 0x00, 0xF3, 0x9C, 0x3F, 0x8F, 0xC1, 0xF8, 0xE7, 0xFF,
0xF1, 0xCE, 0x71, 0xC7, 0xE0, 0x1E, 0x3F, 0x00, 0xF3, 0x9C, 0x3F, 0x8F, 0xC1, 0xF8, 0xE7, 0xFF,
0xF8, 0x0E, 0x03, 0xE0, 0x07, 0x80, 0x00, 0x1C, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x07, 0xFF,
0xF8, 0x1F, 0x03, 0xF0, 0x07, 0x80, 0x00, 0x3C, 0x00, 0x3F, 0x00, 0x00, 0x08, 0x00, 0x0F, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xDF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x77,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x8F, 0xFB,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x02, 0x3F, 0xFB,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00, 0xE7, 0xFE, 0x7F, 0xE6, 0x10, 0x00, 0x08, 0xFF, 0xFB,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x01, 0xE6, 0x06, 0x60, 0x6E, 0x10, 0x00, 0x11, 0xFF, 0xF7,
0xE0, 0x00, 0x00, 0x00, 0x3F, 0xC0, 0x03, 0xEE, 0x06, 0x60, 0x6C, 0x18, 0x00, 0x43, 0xC3, 0xE7,
0xEF, 0xEF, 0x42, 0x3F, 0xBF, 0xC0, 0x03, 0x6E, 0x0E, 0xE0, 0xEC, 0x38, 0x00, 0x83, 0x03, 0xEF,
0xE8, 0x21, 0xA1, 0x20, 0xBF, 0xC0, 0x06, 0xCC, 0x0C, 0xC0, 0xCC, 0x38, 0x03, 0x04, 0x07, 0x9F,
0xEB, 0xA5, 0x53, 0xAE, 0xBF, 0xC0, 0x0C, 0xCC, 0x00, 0xC0, 0xDC, 0x38, 0x04, 0x00, 0x0F, 0x1F,
0xEB, 0xA3, 0x44, 0xAE, 0xBF, 0x80, 0x18, 0xCC, 0x00, 0xC0, 0xDC, 0x38, 0x08, 0x00, 0x1F, 0xDF,
0xEB, 0xA3, 0xBF, 0x2E, 0xBF, 0x80, 0x30, 0xDC, 0x01, 0xC1, 0xD8, 0x3C, 0x10, 0x00, 0x3F, 0xFF,
0xE8, 0x25, 0x06, 0xA0, 0xBF, 0x80, 0x61, 0x98, 0xFD, 0xFF, 0xD8, 0x3C, 0x30, 0x00, 0xFF, 0xDF,
0xEF, 0xEA, 0xAA, 0xBF, 0xBF, 0x80, 0xFF, 0x98, 0x19, 0xFF, 0x18, 0x3C, 0x60, 0x01, 0xC7, 0xDF,
0xE0, 0x0C, 0xD8, 0x00, 0x3F, 0x01, 0xFF, 0x98, 0x19, 0x80, 0x38, 0x3C, 0xC0, 0x02, 0x07, 0xBF,
0xED, 0xA4, 0xAC, 0x20, 0xBF, 0x03, 0x01, 0xB8, 0x19, 0x80, 0x30, 0x1E, 0xC0, 0x00, 0x0F, 0xBF,
0xE8, 0xC2, 0x33, 0x5B, 0x3F, 0x06, 0x03, 0x38, 0x3B, 0x80, 0x30, 0x1D, 0x80, 0x00, 0x1F, 0x7F,
0xEB, 0x7C, 0xF2, 0x1A, 0x3F, 0x0C, 0x03, 0x30, 0x33, 0x80, 0x30, 0x1F, 0x80, 0x00, 0x3E, 0xFF,
0xE7, 0x08, 0x25, 0x74, 0xBF, 0x18, 0x03, 0x3F, 0xF3, 0x00, 0x7F, 0x9B, 0x80, 0x00, 0xFD, 0xFF,
0xEC, 0xFE, 0x11, 0xB0, 0xBE, 0x30, 0x03, 0x1F, 0xE3, 0x00, 0x7F, 0x9F, 0x80, 0x03, 0xFB, 0xFF,
0xE1, 0x0A, 0xFA, 0x3F, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x80, 0x0F, 0xE7, 0xFF,
0xE5, 0x26, 0x73, 0xEA, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xC0, 0x7F, 0xCF, 0xFF,
0xEE, 0x9A, 0x91, 0x1A, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xFF, 0xFF, 0xBF, 0xFF,
0xEA, 0xA9, 0xC1, 0xC4, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0xFF, 0xFE, 0x7F, 0xFF,
0xEC, 0x95, 0x33, 0x0B, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0xFF, 0xF9, 0xFF, 0xFF,
0xEC, 0x2A, 0xB9, 0x0C, 0xBF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0xFF, 0xE7, 0xFF, 0xFF,
0xED, 0x03, 0x73, 0x26, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0x9F, 0xFF, 0xFF,
0xEE, 0xF9, 0x54, 0xFF, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0xF8, 0x7F, 0xFF, 0xFF,
0xE0, 0x0E, 0xF0, 0x8C, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0xFF, 0xFF, 0xFF,
0xEF, 0xE7, 0x55, 0xAC, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xE8, 0x23, 0xCF, 0x89, 0xBF, 0xFF, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xFF, 0xDF, 0xFF, 0xFF, 0xFF,
0xEB, 0xAB, 0xD3, 0xFD, 0x38, 0x7F, 0xFF, 0xFF, 0xE7, 0xF8, 0x7F, 0xFF, 0x9F, 0xFF, 0xFF, 0xFF,
0xEB, 0xA8, 0x68, 0x40, 0xBB, 0xEC, 0xE7, 0xCC, 0xFF, 0x7B, 0xEC, 0xEF, 0x39, 0xE9, 0xFF, 0xFF,
0xEB, 0xA1, 0xD2, 0xDB, 0xB8, 0x8A, 0xD7, 0x2B, 0xE8, 0x78, 0x8A, 0x94, 0xA5, 0xA5, 0xFF, 0xFF,
0xE8, 0x29, 0x10, 0x36, 0xB3, 0xB0, 0x87, 0x49, 0xE9, 0x73, 0xB0, 0x85, 0xAD, 0x2D, 0xFF, 0xFF,
0xEF, 0xEE, 0x23, 0xA8, 0x37, 0xB3, 0xBE, 0x4C, 0xCB, 0x77, 0xB7, 0xBD, 0x2D, 0x6D, 0xFF, 0xFF,
0xE0, 0x00, 0x00, 0x00, 0x37, 0xB8, 0x8F, 0x11, 0xDB, 0x77, 0x38, 0x8C, 0x23, 0x69, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
