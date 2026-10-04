// Host versions of the Teensy core's avr_functions.h helpers used by WString.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
char *ultoa(unsigned long val, char *buf, int radix);
char *ltoa(long val, char *buf, int radix);
char *ulltoa(unsigned long long val, char *buf, int radix);
char *lltoa(long long val, char *buf, int radix);
static inline char *utoa(unsigned int val, char *buf, int radix) { return ultoa(val, buf, radix); }
static inline char *itoa(int val, char *buf, int radix) { return ltoa(val, buf, radix); }
char *dtostrf(float val, int width, unsigned int precision, char *buf);
#ifdef __cplusplus
}
#endif
