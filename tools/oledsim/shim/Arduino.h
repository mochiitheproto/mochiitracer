// Host (x86) stand-in for the Teensy 4 Arduino core, just enough to compile
// Adafruit_GFX, the real Adafruit_SSD1306 and ProtoTracer's HUD code.
// Semantics copied from cores/teensy4 (wiring.h, core_pins.h): millis() and
// micros() are uint32_t, min/max are the same templates, constrain/sq are the
// same statement-expression macros, map() does 32-bit math.
// The clock is VIRTUAL (oledsim_now_us), advanced by the driver, by delay()
// and by I2C transfers in the Wire emulator.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <type_traits>
#include <utility>

#include "avr/pgmspace.h"
#include "avr_functions.h"
#include "core_id.h"
#include "WString.h"
#include "Printable.h"
#include "Print.h"

#ifndef ARDUINO
#define ARDUINO 10819
#endif
#ifndef TEENSYDUINO
#define TEENSYDUINO 159
#endif
#ifndef F_CPU
#define F_CPU 600000000
#endif
#define F_CPU_ACTUAL F_CPU

#define DMAMEM
#define FLASHMEM
#define FASTRUN
#define EXTMEM

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define OUTPUT_OPENDRAIN 4
#define INPUT_DISABLE 5
#define LSBFIRST 0
#define MSBFIRST 1
#define CHANGE 4
#define FALLING 2
#define RISING 3

typedef bool boolean;
typedef uint8_t byte;

// ---- virtual clock -------------------------------------------------------
extern uint64_t oledsim_now_us;
static inline uint32_t millis(void) { return (uint32_t)(oledsim_now_us / 1000ULL); }
static inline uint32_t micros(void) { return (uint32_t)(oledsim_now_us); }
static inline void delay(uint32_t ms) { oledsim_now_us += (uint64_t)ms * 1000ULL; }
static inline void delayMicroseconds(uint32_t us) { oledsim_now_us += us; }
static inline void delayNanoseconds(uint32_t ns) { oledsim_now_us += ns / 1000; }
static inline void yield(void) {}

// ---- Teensy-only helpers a design may call --------------------------------
// tempmonGetTemp(): chip temperature in °C. In the sim it returns the "temp"
// field of the current scenario state (default 46.0).
float tempmonGetTemp(void);
static inline void tempmon_init(void) {}

static inline void pinMode(uint8_t, uint8_t) {}
static inline void digitalWrite(uint8_t, uint8_t) {}
static inline void digitalWriteFast(uint8_t, uint8_t) {}
static inline uint8_t digitalRead(uint8_t) { return HIGH; }  // button not pressed
static inline uint8_t digitalReadFast(uint8_t) { return HIGH; }
static inline int analogRead(uint8_t) { return 0; }
static inline void analogWrite(uint8_t, int) {}
static inline void analogReadResolution(unsigned int) {}
static inline void analogWriteResolution(unsigned int) {}
static inline void analogWriteFrequency(uint8_t, float) {}
static inline void __disable_irq(void) {}
static inline void __enable_irq(void) {}
#define interrupts() __enable_irq()
#define noInterrupts() __disable_irq()
#define sei() __enable_irq()
#define cli() __disable_irq()

// ---- wiring.h ---------------------------------------------------------------
template <class T, class A, class B, class C, class D>
long map(T _x, A _in_min, B _in_max, C _out_min, D _out_max,
         typename std::enable_if<std::is_integral<T>::value>::type* = 0) {
    // Teensy: all math as 32-bit signed long
    int32_t x = _x, in_min = _in_min, in_max = _in_max, out_min = _out_min, out_max = _out_max;
    int32_t in_range = in_max - in_min;
    int32_t out_range = out_max - out_min;
    if (in_range == 0) return out_min + out_range / 2;
    int32_t num = (x - in_min) * out_range;
    if (out_range >= 0) num += in_range / 2; else num -= in_range / 2;
    int32_t result = num / in_range + out_min;
    if (out_range >= 0) { if (in_range * num < 0) return result - 1; }
    else { if (in_range * num >= 0) return result + 1; }
    return result;
}
template <class T, class A, class B, class C, class D>
T map(T x, A in_min, B in_max, C out_min, D out_max,
      typename std::enable_if<std::is_floating_point<T>::value>::type* = 0) {
    return (x - (T)in_min) * ((T)out_max - (T)out_min) / ((T)in_max - (T)in_min) + (T)out_min;
}

template<class A, class B>
constexpr auto min(A&& a, B&& b) -> decltype(a < b ? std::forward<A>(a) : std::forward<B>(b)) {
    return a < b ? std::forward<A>(a) : std::forward<B>(b);
}
template<class A, class B>
constexpr auto max(A&& a, B&& b) -> decltype(a < b ? std::forward<A>(a) : std::forward<B>(b)) {
    return a >= b ? std::forward<A>(a) : std::forward<B>(b);
}

#ifdef PI
#undef PI
#endif
#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define constrain(amt, low, high) ({ \
  __typeof__(amt) _amt = (amt); \
  __typeof__(low) _low = (low); \
  __typeof__(high) _high = (high); \
  (_amt < _low) ? _low : ((_amt > _high) ? _high : _amt); \
})
#define radians(deg) ((deg)*DEG_TO_RAD)
#define degrees(rad) ((rad)*RAD_TO_DEG)
#define sq(x) ({ __typeof__(x) _x = (x); _x * _x; })
#define lowByte(w) ((uint8_t)((w) & 0xFF))
#define highByte(w) ((uint8_t)((w) >> 8))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet((value), (bit)) : bitClear((value), (bit)))
#define bit(b) (1UL << (b))
#define stricmp(a, b) strcasecmp(a, b)

// Teensy WMath.cpp PRNG (avr-libc Park-Miller), deterministic. random(void)
// itself stays glibc's (its declaration clashes on x86), the ranged forms
// are Teensy's.
uint32_t random(uint32_t howbig);
int32_t random(int32_t howsmall, int32_t howbig);
void randomSeed(uint32_t newseed);

// ---- Serial: a Print that discards (or goes to stderr with OLEDSIM_SERIAL=1)
class usb_serial_class : public Print {
public:
    void begin(long) {}
    void end() {}
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    void flush() {}
    using Print::write;
    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    operator bool() { return true; }
};
extern usb_serial_class Serial;

// ---- elapsedMillis / elapsedMicros (cores/teensy4/elapsedMillis.h) --------
class elapsedMillis {
private:
    unsigned long ms;
public:
    elapsedMillis(void) { ms = millis(); }
    elapsedMillis(unsigned long val) { ms = millis() - val; }
    elapsedMillis(const elapsedMillis &orig) { ms = orig.ms; }
    operator unsigned long () const { return (uint32_t)(millis() - ms); }
    elapsedMillis & operator = (const elapsedMillis &rhs) { ms = rhs.ms; return *this; }
    elapsedMillis & operator = (unsigned long val) { ms = millis() - val; return *this; }
    elapsedMillis & operator -= (unsigned long val) { ms += val; return *this; }
    elapsedMillis & operator += (unsigned long val) { ms -= val; return *this; }
};
class elapsedMicros {
private:
    unsigned long us;
public:
    elapsedMicros(void) { us = micros(); }
    elapsedMicros(unsigned long val) { us = micros() - val; }
    elapsedMicros(const elapsedMicros &orig) { us = orig.us; }
    operator unsigned long () const { return (uint32_t)(micros() - us); }
    elapsedMicros & operator = (const elapsedMicros &rhs) { us = rhs.us; return *this; }
    elapsedMicros & operator = (unsigned long val) { us = micros() - val; return *this; }
    elapsedMicros & operator -= (unsigned long val) { us += val; return *this; }
    elapsedMicros & operator += (unsigned long val) { us -= val; return *this; }
};
