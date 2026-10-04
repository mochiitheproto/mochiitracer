// Host shim of avr/pgmspace.h. On Teensy 4 flash is memory-mapped, so all of
// this is identity; same here.
#pragma once
#include <string.h>
#include <stdint.h>
#ifndef PROGMEM
#define PROGMEM
#endif
#define PGM_P const char *
#define PGM_VOID_P const void *
#define PSTR(str) (str)
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_float(addr) (*(const float *)(addr))
#define pgm_read_ptr(addr) (*(const void * const *)(addr))
#define pgm_read_byte_near(addr) pgm_read_byte(addr)
#define pgm_read_word_near(addr) pgm_read_word(addr)
#define pgm_read_dword_near(addr) pgm_read_dword(addr)
#define pgm_read_byte_far(addr) pgm_read_byte(addr)
#define pgm_read_word_far(addr) pgm_read_word(addr)
#define strlen_P(s) strlen((const char *)(s))
#define strcpy_P(d, s) strcpy((d), (const char *)(s))
#define strncpy_P(d, s, n) strncpy((d), (const char *)(s), (n))
#define strcmp_P(a, b) strcmp((a), (const char *)(b))
#define strncmp_P(a, b, n) strncmp((a), (const char *)(b), (n))
#define strcat_P(d, s) strcat((d), (const char *)(s))
#define memcpy_P(d, s, n) memcpy((d), (s), (n))
#define sprintf_P sprintf
#define snprintf_P snprintf
