// Simulator runtime: virtual clock, per-state inputs read by the shims
// (tempmonGetTemp, ...), and the SSD1306 controller emulator fed by Wire.
#pragma once
#include <stdint.h>
#include <string>

struct OledSimInputs {
    float temp = 46.0f;   // returned by tempmonGetTemp()
    bool booped = false;  // passed to HeadsUpDisplay::SetBooped(bool) if the design has it
    float fps = 60.0f;    // passed to HeadsUpDisplay::SetFPS(float) if the design has it
};
extern OledSimInputs oledsim_in;
extern uint64_t oledsim_now_us;

// What the panel physically shows (after invert / display on-off / remaps).
struct OledFrame {
    uint8_t px[64][128];      // 1 = lit
    uint8_t contrast;         // 0x81 value (Adafruit init: 0xCF; dim(true): 0x00)
    bool on;                  // AF/AE and charge pump
    bool invert;              // A7
    uint64_t lastDataUs;      // end of the last GDDRAM write (i.e. last display())
    uint32_t dataBytes;       // total GDDRAM bytes written so far
    uint64_t lastTxUs;        // end of the last transmission to the panel (data OR command)
};

// Traffic to the panel (0x3C on Wire), cumulative since power on. The driver diffs two
// snapshots to see what one loop iteration sent.
struct OledBusStats {
    uint32_t tx;              // transmissions (beginTransmission..endTransmission)
    uint64_t busUs;           // their bus time
    uint32_t dataBytes;       // GDDRAM bytes (control bytes not counted)
    uint32_t cmdBytes;        // command bytes incl. arguments
    uint32_t bursts;          // display()-like bursts: data right after a command transmission
    uint64_t firstTxStartUs;  // start of the first transmission since oledsim_bus_mark() (UINT64_MAX = none)
};
void oledsim_bus_stats(OledBusStats *s);
void oledsim_bus_mark();

void oledsim_snapshot(OledFrame *f);
bool oledsim_same(const OledFrame &a, const OledFrame &b);
std::string oledsim_warnings();
// Raw controller GDDRAM (page-major, exactly what display() pushed).
const uint8_t *oledsim_gddram();
