// Shim mínimo de Arduino para compilar el motor de ProtoTracer en host (x86).
// El reloj es virtual: millis()/micros() leen hostclock::us, que el driver
// avanza a mano (y delay() también lo avanza, para que el frameLimiter no se
// cuelgue).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include "WString.h"

typedef uint8_t byte;
typedef bool boolean;

#define PROGMEM
#define DMAMEM
#define FLASHMEM
#define F(x) (x)
#define __FlashStringHelper char
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#define pgm_read_float(addr) (*(const float*)(addr))

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

namespace hostclock {
    inline uint64_t& us() { static uint64_t v = 0; return v; }
}

inline unsigned long millis() { return (unsigned long)(hostclock::us() / 1000ULL); }
inline unsigned long micros() { return (unsigned long)(hostclock::us()); }
inline void delay(unsigned long ms) { hostclock::us() += (uint64_t)ms * 1000ULL; }
inline void delayMicroseconds(unsigned int us) { hostclock::us() += us; }
inline void yield() {}

template<class T, class L, class H>
inline T constrain(T x, L lo, H hi) { return x < (T)lo ? (T)lo : (x > (T)hi ? (T)hi : x); }
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
inline double radians(double d) { return d * DEG_TO_RAD; }
inline double degrees(double r) { return r * RAD_TO_DEG; }
inline long random(long hi) { return hi > 0 ? std::rand() % hi : 0; }
inline long random(long lo, long hi) { return hi > lo ? lo + std::rand() % (hi - lo) : lo; }
inline void randomSeed(unsigned long s) { std::srand((unsigned)s); }
using std::min;
using std::max;
using std::abs;

struct HostSerial {
    void begin(unsigned long) {}
    int available() { return 0; }
    int read() { return -1; }
    template<class T> void print(const T&) {}
    template<class T> void print(const T&, int) {}
    template<class T> void println(const T&) {}
    template<class T> void println(const T&, int) {}
    void println() {}
    operator bool() const { return true; }
};
static HostSerial Serial;
