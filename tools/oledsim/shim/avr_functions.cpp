#include "avr_functions.h"
#include <stdio.h>
#include <string.h>

extern "C" {
char *ulltoa(unsigned long long val, char *buf, int radix) {
    char tmp[72]; int i = 0;
    if (radix < 2 || radix > 36) radix = 10;
    do { int d = (int)(val % radix); tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10; val /= radix; } while (val);
    int j = 0; while (i) buf[j++] = tmp[--i]; buf[j] = 0; return buf;
}
char *lltoa(long long val, char *buf, int radix) {
    if (val < 0) { buf[0] = '-'; ulltoa((unsigned long long)(-val), buf + 1, radix); return buf; }
    return ulltoa((unsigned long long)val, buf, radix);
}
// Teensy 4: long is 32 bits. Keep the same wrap-around so String((long)x) matches.
char *ultoa(unsigned long val, char *buf, int radix) { return ulltoa((uint32_t)val, buf, radix); }
char *ltoa(long val, char *buf, int radix) { return lltoa((int32_t)val, buf, radix); }
char *dtostrf(float val, int width, unsigned int precision, char *buf) {
    sprintf(buf, "%*.*f", width, precision, (double)val);
    return buf;
}
}
