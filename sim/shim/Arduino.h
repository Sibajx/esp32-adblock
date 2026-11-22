// Simulador: lo mínimo de Arduino/ESP32 que usa el firmware, sobre Linux.
#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define PROGMEM

#define LOW 0
#define HIGH 1
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
// GPIO0 (BOOT) es LOW si existe $SIM_FLASH/.boot; digitalWrite(2) imprime "[sim] led <0|1>".
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t val);

uint32_t millis();
void delay(uint32_t ms);
inline void yield() {}

class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  int indexOf(char c) const { size_t p = s_.find(c); return p == std::string::npos ? -1 : int(p); }
  int lastIndexOf(char c) const { size_t p = s_.rfind(c); return p == std::string::npos ? -1 : int(p); }
  String substring(size_t a, size_t b) const { return String(s_.substr(a, b > a ? b - a : 0).c_str()); }
  void toLowerCase() { for (char& c : s_) c = char(tolower((unsigned char)c)); }
  void trim() {  // como el de Arduino: espacios, tabs y saltos de línea a los dos lados
    size_t a = s_.find_first_not_of(" \t\r\n\f\v"), b = s_.find_last_not_of(" \t\r\n\f\v");
    s_ = a == std::string::npos ? std::string() : s_.substr(a, b - a + 1);
  }
  void reserve(size_t n) { s_.reserve(n); }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += o; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  friend String operator+(String a, const String& b) { return a += b; }
  friend String operator+(String a, const char* b) { return a += b; }
  friend String operator+(const char* a, const String& b) { return String(a) += b; }
  bool operator==(const char* o) const { return s_ == o; }
  bool operator!=(const char* o) const { return s_ != o; }
  bool operator==(const String& o) const { return s_ == o.s_; }
  const std::string& str() const { return s_; }

 private:
  std::string s_;
};

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { b_[0] = a; b_[1] = b; b_[2] = c; b_[3] = d; }
  explicit IPAddress(uint32_t raw) { memcpy(b_, &raw, 4); }  // como lwIP: bytes en orden de red
  operator uint32_t() const { uint32_t r; memcpy(&r, b_, 4); return r; }
  uint8_t operator[](int i) const { return b_[i]; }
  String toString() const {
    char s[16];
    snprintf(s, sizeof s, "%u.%u.%u.%u", b_[0], b_[1], b_[2], b_[3]);
    return String(s);
  }

 private:
  uint8_t b_[4] = {0, 0, 0, 0};
};

// Salida a stdout con un mutex (una línea no se mezcla con otra). Entrada: si SIM_SERIAL_STDIN=1,
// available()/read() leen stdin sin bloquear; si no, no llega nada.
struct SerialPort {
  void begin(unsigned long) {}
  void setRxBufferSize(size_t) {}
  int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  void print(const char* s);
  void print(char c);
  void println(const char* s = "");
  int available();
  int read();
  size_t write(const uint8_t* buf, size_t n);
  size_t write(uint8_t b) { return write(&b, 1); }
};
extern SerialPort Serial;

struct EspClass {
  uint32_t getFreeHeap();
  uint32_t getMaxAllocHeap() { return getFreeHeap() * 3 / 5; }  // aprox.: el heap real está partido
  [[noreturn]] void restart();
};
extern EspClass ESP;

// Cuenta la RAM pedida con new/malloc en el proceso (aprox. del heap del ESP32).
size_t simHeapInUse();

// NTP: en el simulador la hora ya es la de la PC.
inline void configTime(long, int, const char*, const char* = nullptr, const char* = nullptr) {}
