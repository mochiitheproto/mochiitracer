#include "SSD1306.h"
Adafruit_SSD1306 HeadsUpDisplay::display(128, 64, &Wire, -1);

void HeadsUpDisplay::Initialize() {
    Wire.setClock(100000);
    Wire.begin();
    Wire.beginTransmission(0x3C);
    if (Wire.endTransmission() == 0) didBegin = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.display();
}

void HeadsUpDisplay::SetBoopGesture(uint8_t s, uint16_t h, uint8_t e) {
    stage = s;
    holdMs = h;
    if (e != BoopGesture::NONE && e != BoopGesture::GLITCH && e != BoopGesture::CANCEL_TAP_SHORT) {
        lastEvent = e;
        lastEventAt = millis();
    }
}

void HeadsUpDisplay::Update() {
    if (!didBegin) return;
    uint32_t now = millis();
    bool active = stage >= BoopGesture::STAGE_TAP || (lastEvent && now - lastEventAt < 1500);
    if (now - lastDraw < (active ? 100u : 200u)) return;
    lastDraw = now;

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(2, 2);
    display.print(faceNames[Menu::GetFaceState()]);
    if (stage == BoopGesture::STAGE_TAP || stage == BoopGesture::STAGE_GAP) display.fillCircle(10, 45, 6, SSD1306_WHITE);
    if (stage == BoopGesture::STAGE_HOLD) {
        display.drawRect(4, 38, 120, 14, SSD1306_WHITE);
        display.fillRect(6, 40, (uint32_t)116 * (holdMs > 800 ? 800 : holdMs) / 800, 10, SSD1306_WHITE);
    }
    if (lastEvent && now - lastEventAt < 1500 && stage != BoopGesture::STAGE_HOLD) {
        display.setCursor(28, 38);
        display.print(lastEvent == BoopGesture::FIRE ? F("SI!") : (lastEvent <= BoopGesture::HOLD_START ? F("...") : F("nel")));
    }
    display.display();
}
