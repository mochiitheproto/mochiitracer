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

void HeadsUpDisplay::Update() {
    if (!timeStep.IsReady() || !didBegin) return;
    display.clearDisplay();
    display.dim(Menu::GetBrightness() < 2);
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(2, 16);
    display.print(faceNames[Menu::GetFaceState()]);
    float t = tempmonGetTemp();
    if (t >= 72.0f ? (millis() / 250) % 2 : t >= 60.0f) display.drawBitmap(110, 44, thermoIcon, 16, 16, SSD1306_WHITE);
    display.setFont(nullptr);
    display.setCursor(2, 54);
    if (booped) display.print(F("boop!"));
    display.print(' ');
    display.print((int)fps);
    display.display();
}
