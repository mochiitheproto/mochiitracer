// Host stub of the Teensy SPI library: present so the real Adafruit_SSD1306
// compiles. The HUD uses I2C; nothing here is ever executed.
#pragma once
#include <Arduino.h>
#define SPI_HAS_TRANSACTION 1
#define SPI_MODE0 0x00
#define SPI_MODE1 0x04
#define SPI_MODE2 0x08
#define SPI_MODE3 0x0C
class SPISettings {
public:
    SPISettings() {}
    SPISettings(uint32_t, uint8_t, uint8_t) {}
};
class SPIClass {
public:
    void begin() {}
    void end() {}
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
    uint8_t transfer(uint8_t) { return 0; }
    uint16_t transfer16(uint16_t) { return 0; }
    void transfer(void *, size_t) {}
    void setMOSI(uint8_t) {}
    void setMISO(uint8_t) {}
    void setSCK(uint8_t) {}
};
extern SPIClass SPI;
