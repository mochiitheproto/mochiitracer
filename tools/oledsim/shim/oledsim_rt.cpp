#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <set>
#include <string>
#include "oledsim_rt.h"

uint64_t oledsim_now_us = 0;
OledSimInputs oledsim_in;

float tempmonGetTemp(void) { return oledsim_in.temp; }

// ---- Serial ------------------------------------------------------------------
usb_serial_class Serial;
static bool serialToStderr() {
    static int v = -1;
    if (v < 0) { const char *e = getenv("OLEDSIM_SERIAL"); v = (e && *e == '1') ? 1 : 0; }
    return v == 1;
}
size_t usb_serial_class::write(uint8_t b) {
    if (serialToStderr()) fputc(b, stderr);
    return 1;
}
size_t usb_serial_class::write(const uint8_t *buffer, size_t size) {
    if (serialToStderr()) fwrite(buffer, 1, size, stderr);
    return size;
}

// ---- Teensy WMath.cpp --------------------------------------------------------
static uint32_t prngSeed;
void randomSeed(uint32_t newseed) { if (newseed > 0) prngSeed = newseed; }
static int32_t teensyRandom(void) {
    int32_t hi, lo, x;
    x = prngSeed;
    if (x == 0) x = 123459876;
    hi = x / 127773;
    lo = x % 127773;
    x = 16807 * lo - 2836 * hi;
    if (x < 0) x += 0x7FFFFFFF;
    prngSeed = x;
    return x;
}
uint32_t random(uint32_t howbig) { return howbig == 0 ? 0 : (uint32_t)teensyRandom() % howbig; }
int32_t random(int32_t howsmall, int32_t howbig) {
    if (howsmall >= howbig) return howsmall;
    return (int32_t)random((uint32_t)(howbig - howsmall)) + howsmall;
}

SPIClass SPI;

// ---- SSD1306 controller emulator --------------------------------------------
// Parses the I2C byte stream exactly as the chip does (control byte Co/D#C,
// command arguments that may span transmissions, horizontal/vertical/page
// addressing) and keeps GDDRAM + display registers. Power-on defaults are the
// datasheet reset values.
namespace {
struct Ssd1306Emu {
    uint8_t ram[8][128] = {};
    bool on = false, invert = false, entireOn = false, segRemap = false, comDec = false;
    bool chargePump = false;
    uint8_t contrast = 0x7F, startLine = 0, offset = 0, mux = 63, memMode = 2, comPins = 0x12;
    uint8_t colStart = 0, colEnd = 127, pageStart = 0, pageEnd = 7, col = 0, page = 0;
    uint8_t cmd = 0, need = 0, got = 0, args[8] = {};
    uint64_t lastDataUs = 0;
    uint64_t lastTxUs = 0;
    uint32_t dataBytes = 0;
    uint32_t cmdBytes = 0;
    uint32_t bursts = 0;
    bool lastTxData = false;
    std::set<std::string> warnings;

    static uint8_t argCount(uint8_t c) {
        switch (c) {
            case 0x20: case 0x81: case 0x8D: case 0xA8: case 0xD3: case 0xD5:
            case 0xD9: case 0xDA: case 0xDB: case 0x23: case 0xD6: return 1;
            case 0x21: case 0x22: case 0xA3: return 2;
            case 0x29: case 0x2A: return 5;
            case 0x26: case 0x27: return 6;
            default: return 0;
        }
    }

    void command(uint8_t b) {
        cmdBytes++;
        if (need) {
            args[got++] = b;
            if (got >= need) { need = 0; execute(cmd); }
            return;
        }
        cmd = b; got = 0; need = argCount(b);
        if (!need) execute(b);
    }

    void execute(uint8_t c) {
        if (c <= 0x0F) { col = (col & 0xF0) | (c & 0x0F); return; }
        if (c <= 0x1F) { col = (uint8_t)(((c & 0x0F) << 4) | (col & 0x0F)) & 0x7F; return; }
        if (c >= 0x40 && c <= 0x7F) { startLine = c & 0x3F; if (startLine) warnings.insert("start line != 0"); return; }
        if (c >= 0xB0 && c <= 0xB7) { page = c & 7; return; }
        switch (c) {
            case 0x20: memMode = args[0] & 3; break;
            case 0x21: colStart = args[0] & 0x7F; colEnd = args[1] & 0x7F; col = colStart; break;
            case 0x22: pageStart = args[0] & 7; pageEnd = args[1] & 7; page = pageStart; break;
            case 0x81: contrast = args[0]; break;
            case 0x8D: chargePump = (args[0] & 0x04) != 0; break;
            case 0xA8: mux = args[0] & 0x3F; if (mux != 63) warnings.insert("multiplex != 64 lines"); break;
            case 0xD3: offset = args[0] & 0x3F; if (offset) warnings.insert("display offset != 0"); break;
            case 0xDA: comPins = args[0]; if ((comPins & 0x30) != 0x10) warnings.insert("COM pins config != 0x12 (rows would interleave on a 128x64 module; not emulated)"); break;
            case 0xA0: case 0xA1: segRemap = c & 1; break;
            case 0xA4: case 0xA5: entireOn = c & 1; break;
            case 0xA6: case 0xA7: invert = c & 1; break;
            case 0xAE: case 0xAF: on = c & 1; break;
            case 0xC0: comDec = false; break;
            case 0xC8: comDec = true; break;
            case 0x2F: warnings.insert("hardware scroll activated (not emulated)"); break;
            case 0x23: if (args[0] & 0x30) warnings.insert("fade/blink mode (not emulated)"); break;
            case 0xD6: if (args[0] & 1) warnings.insert("zoom-in mode (not emulated)"); break;
            default: break; // timing / precharge / vcomh / scroll setup / NOP
        }
    }

    void data(uint8_t b) {
        ram[page & 7][col & 0x7F] = b;
        dataBytes++;
        switch (memMode) {
            case 0: // horizontal
                if (col >= colEnd) { col = colStart; page = (page >= pageEnd) ? pageStart : page + 1; }
                else col++;
                break;
            case 1: // vertical
                if (page >= pageEnd) { page = pageStart; col = (col >= colEnd) ? colStart : col + 1; }
                else page++;
                break;
            default: // page addressing
                col = (col >= 127) ? 0 : col + 1;
                break;
        }
    }

    void transmission(const uint8_t *p, int n) {
        int i = 0;
        bool wroteData = false;
        while (i < n) {
            uint8_t ctrl = p[i++];
            bool co = ctrl & 0x80, dc = ctrl & 0x40;
            if (co) {
                if (i < n) { if (dc) { data(p[i]); wroteData = true; } else command(p[i]); i++; }
            } else {
                while (i < n) { if (dc) { data(p[i]); wroteData = true; } else command(p[i]); i++; }
            }
        }
        if (wroteData) {
            lastDataUs = oledsim_now_us;
            if (!lastTxData) bursts++;
        }
        lastTxData = wroteData;
        lastTxUs = oledsim_now_us;
    }

    // Reference orientation = Adafruit init (SEGREMAP|1 = A1, COMSCANDEC = C8):
    // buffer (x, y) shows at physical (x, y), y = 0 at the top.
    bool visible(int x, int y) const {
        if (!on || !chargePump) return false;
        if (entireOn) return true;
        int line = comDec ? y : 63 - y;
        if (line > mux) return false;
        int r = (line + startLine + offset) & 63;
        int c = segRemap ? x : 127 - x;
        bool bit = (ram[r >> 3][c] >> (r & 7)) & 1;
        return bit != invert;
    }
};

Ssd1306Emu emu;
uint64_t busNsRemainder = 0;
uint32_t panelTx = 0;
uint64_t panelBusUs = 0;
uint64_t firstTxStartUs = UINT64_MAX;
}

void oledsim_snapshot(OledFrame *f) {
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 128; x++) f->px[y][x] = emu.visible(x, y) ? 1 : 0;
    f->contrast = emu.contrast;
    f->on = emu.on && emu.chargePump;
    f->invert = emu.invert;
    f->lastDataUs = emu.lastDataUs;
    f->dataBytes = emu.dataBytes;
    f->lastTxUs = emu.lastTxUs;
}

void oledsim_bus_stats(OledBusStats *s) {
    s->tx = panelTx;
    s->busUs = panelBusUs;
    s->dataBytes = emu.dataBytes;
    s->cmdBytes = emu.cmdBytes;
    s->bursts = emu.bursts;
    s->firstTxStartUs = firstTxStartUs;
}

void oledsim_bus_mark() { firstTxStartUs = UINT64_MAX; }

bool oledsim_same(const OledFrame &a, const OledFrame &b) {
    return a.contrast == b.contrast && a.on == b.on && memcmp(a.px, b.px, sizeof(a.px)) == 0;
}

std::string oledsim_warnings() {
    std::string s;
    for (auto &w : emu.warnings) { if (!s.empty()) s += "; "; s += w; }
    return s;
}

const uint8_t *oledsim_gddram() { return &emu.ram[0][0]; }

// ---- Wire --------------------------------------------------------------------
TwoWire Wire(0);
TwoWire Wire1(1);
TwoWire Wire2(2);

uint8_t TwoWire::endTransmission(uint8_t) {
    if (!transmitting) return 4;
    transmitting = false;
    // bus time: address + payload, 9 clocks per byte, ~2 clocks start/stop
    uint64_t bits = (uint64_t)(1 + txLen) * 9 + 2;
    uint64_t ns = bits * 1000000000ULL / clock + busNsRemainder;
    const uint64_t startUs = oledsim_now_us;
    oledsim_now_us += ns / 1000;
    busNsRemainder = ns % 1000;
    if (bus == 0 && txAddress == 0x3C) {
        panelTx++;
        panelBusUs += oledsim_now_us - startUs;
        if (firstTxStartUs == UINT64_MAX) firstTxStartUs = startUs;
        emu.transmission(txBuffer, txLen);
        return 0;
    }
    return 2; // address NACK: nothing else on the simulated bus
}

uint8_t TwoWire::requestFrom(uint8_t, uint8_t, uint8_t) {
    rxLen = rxIndex = 0;
    return 0;
}
