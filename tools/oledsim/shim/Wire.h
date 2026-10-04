// Host stand-in for the Teensy 4 Wire library. Transmissions addressed to
// 0x3C are fed byte-for-byte into an SSD1306 controller emulator
// (oledsim_rt.cpp), which is what the simulator "photographs". Every
// transmission advances the virtual clock by its real bus time
// ((1 + n) * 9 bits + start/stop at the current clock: ~23 ms per full frame
// at the 400 kHz the Adafruit lib switches to during display()).
#pragma once
#include <Arduino.h>

#define BUFFER_LENGTH 136   // WireIMXRT.h (Teensy 4) -> WIRE_MAX = 136 in Adafruit_SSD1306
#define WIRE_HAS_END 1
#define WIRE_IMPLEMENT_WIRE

class TwoWire : public Print {
public:
    explicit TwoWire(uint8_t bus) : bus(bus) {}
    void begin() { began = true; }
    void begin(uint8_t) { began = true; }
    void begin(int a) { begin((uint8_t)a); }
    void end() { began = false; }
    void setClock(uint32_t frequency) { clock = frequency ? frequency : 100000; }
    void setSDA(uint8_t) {}
    void setSCL(uint8_t) {}
    void beginTransmission(uint8_t address) { txAddress = address; txLen = 0; transmitting = true; }
    void beginTransmission(int address) { beginTransmission((uint8_t)address); }
    uint8_t endTransmission(uint8_t sendStop);
    uint8_t endTransmission(void) { return endTransmission(1); }
    uint8_t requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop);
    uint8_t requestFrom(uint8_t address, uint8_t quantity) { return requestFrom(address, quantity, 1); }
    uint8_t requestFrom(int address, int quantity, int sendStop) { return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)sendStop); }
    uint8_t requestFrom(int address, int quantity) { return requestFrom((uint8_t)address, (uint8_t)quantity, 1); }
    using Print::write;
    size_t write(uint8_t data) override {
        if (!transmitting || txLen >= BUFFER_LENGTH) return 0;
        txBuffer[txLen++] = data;
        return 1;
    }
    size_t write(const uint8_t *data, size_t quantity) override {
        size_t n = 0;
        while (quantity--) n += write(*data++);
        return n;
    }
    int available(void) { return rxLen - rxIndex; }
    int read(void) { return rxIndex < rxLen ? rxBuffer[rxIndex++] : -1; }
    int peek(void) { return rxIndex < rxLen ? rxBuffer[rxIndex] : -1; }
    void flush(void) {}
    uint32_t getClock() const { return clock; }
private:
    uint8_t bus;
    bool began = false;
    bool transmitting = false;
    uint32_t clock = 100000;
    uint8_t txAddress = 0;
    uint8_t txBuffer[BUFFER_LENGTH + 1] = {};
    uint8_t txLen = 0;
    uint8_t rxBuffer[BUFFER_LENGTH] = {};
    uint8_t rxLen = 0, rxIndex = 0;
};

extern TwoWire Wire;
extern TwoWire Wire1;
extern TwoWire Wire2;
