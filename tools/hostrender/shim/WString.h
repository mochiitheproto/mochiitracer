// Shim mínimo de Arduino String para compilar ProtoTracer en host (x86).
#pragma once
#include <string>
#include <cstdio>
#include <cstdint>

class String {
public:
    std::string s;
    String() {}
    String(const char* c) : s(c ? c : "") {}
    String(const std::string& c) : s(c) {}
    String(char c) : s(1, c) {}
    String(int v) : s(std::to_string(v)) {}
    String(unsigned int v) : s(std::to_string(v)) {}
    String(long v) : s(std::to_string(v)) {}
    String(unsigned long v) : s(std::to_string(v)) {}
    String(unsigned char v) : s(std::to_string((unsigned)v)) {}
    String(float v, int dec = 2) { fmt(v, dec); }
    String(double v, int dec = 2) { fmt(v, dec); }
    const char* c_str() const { return s.c_str(); }
    unsigned int length() const { return s.size(); }
    String& operator+=(const String& o) { s += o.s; return *this; }
    String& operator+=(const char* o) { s += o; return *this; }
    String& operator+=(char o) { s += o; return *this; }
    friend String operator+(const String& a, const String& b) { return String(a.s + b.s); }
    friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
    friend String operator+(const String& a, const char* b) { return String(a.s + b); }
    friend String operator+(const String& a, char b) { return String(a.s + b); }
    bool operator==(const String& o) const { return s == o.s; }
private:
    void fmt(double v, int dec) {
        char b[64];
        snprintf(b, sizeof(b), "%.*f", dec, v);
        s = b;
    }
};
