/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2017 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// Host port of the Teensy 4 core Print.cpp (same algorithms).
#include <Arduino.h>
#include <math.h>

size_t Print::write(const uint8_t *buffer, size_t size)
{
	if (buffer == nullptr) return 0;
	size_t count = 0;
	while (size--) count += write(*buffer++);
	return count;
}

size_t Print::print(const String &s)
{
	uint8_t buffer[33];
	size_t count = 0;
	unsigned int index = 0;
	unsigned int len = s.length();
	while (len > 0) {
		s.getBytes(buffer, sizeof(buffer), index);
		unsigned int nbytes = len;
		if (nbytes > sizeof(buffer)-1) nbytes = sizeof(buffer)-1;
		index += nbytes;
		len -= nbytes;
		count += write(buffer, nbytes);
	}
	return count;
}

size_t Print::print(long n)
{
	// Teensy 4: long is 32 bits
	int32_t v = (int32_t)n;
	uint8_t sign = 0;
	if (v < 0) { sign = '-'; v = -v; }
	return printNumber((uint32_t)v, 10, sign);
}

size_t Print::print(long long n)
{
	if (n < 0) return printNumber64(-n, 10, 1);
	return printNumber64(n, 10, 0);
}

size_t Print::println(void)
{
	uint8_t buf[2]={'\r', '\n'};
	return write(buf, 2);
}

int Print::vprintf(const char *format, va_list ap)
{
	char buf[512];
	int n = vsnprintf(buf, sizeof(buf), format, ap);
	if (n > 0) write((const uint8_t *)buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
	return n;
}

int Print::printf(const char *format, ...)
{
	va_list ap; va_start(ap, format);
	int r = vprintf(format, ap);
	va_end(ap); return r;
}

int Print::printf(const __FlashStringHelper *format, ...)
{
	va_list ap; va_start(ap, format);
	int r = vprintf((const char *)format, ap);
	va_end(ap); return r;
}

size_t Print::printNumber(unsigned long n, uint8_t base, uint8_t sign)
{
	uint8_t buf[34];
	uint8_t digit, i;
	n = (uint32_t)n;  // Teensy 4: unsigned long is 32 bits
	if (base == 0) {
		return write((uint8_t)n);
	} else if (base == 1) {
		base = 10;
	}
	if (n == 0) {
		buf[sizeof(buf) - 1] = '0';
		i = sizeof(buf) - 1;
	} else {
		i = sizeof(buf) - 1;
		while (1) {
			digit = n % base;
			buf[i] = ((digit < 10) ? '0' + digit : 'A' + digit - 10);
			n /= base;
			if (n == 0) break;
			i--;
		}
	}
	if (sign) {
		i--;
		buf[i] = '-';
	}
	return write(buf + i, sizeof(buf) - i);
}

size_t Print::printNumber64(uint64_t n, uint8_t base, uint8_t sign)
{
	uint8_t buf[66];
	uint8_t digit, i;
	if (base < 2) return 0;
	if (n == 0) {
		buf[sizeof(buf) - 1] = '0';
		i = sizeof(buf) - 1;
	} else {
		i = sizeof(buf) - 1;
		while (1) {
			digit = n % base;
			buf[i] = ((digit < 10) ? '0' + digit : 'A' + digit - 10);
			n /= base;
			if (n == 0) break;
			i--;
		}
	}
	if (sign) {
		i--;
		buf[i] = '-';
	}
	return write(buf + i, sizeof(buf) - i);
}

size_t Print::printFloat(double number, uint8_t digits)
{
	uint8_t sign=0;
	size_t count=0;
	if (isnan(number)) return print("nan");
	if (isinf(number)) return print("inf");
	if (number > 4294967040.0f) return print("ovf");
	if (number <-4294967040.0f) return print("ovf");
	if (number < 0.0) {
		sign = 1;
		number = -number;
	}
	double rounding = 0.5;
	for (uint8_t i=0; i<digits; ++i) {
		rounding *= 0.1;
	}
	number += rounding;
	unsigned long int_part = (unsigned long)number;
	double remainder = number - (double)int_part;
	count += printNumber(int_part, 10, sign);
	if (digits > 0) {
		uint8_t n, buf[16], count=1;
		buf[0] = '.';
		if (digits > sizeof(buf) - 1) digits = sizeof(buf) - 1;
		while (digits-- > 0) {
			remainder *= 10.0;
			n = (uint8_t)(remainder);
			buf[count++] = '0' + n;
			remainder -= n;
		}
		count += write(buf, count);
	}
	return count;
}
