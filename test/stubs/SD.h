#pragma once
#include <Arduino.h>
#include <SPI.h>
// Host stub for the Arduino SD library. Only the surface the firmware
// actually uses - extend it when the firmware starts using more, which is
// what happened when the characterisation import arrived and needed
// read/write/close/available/readBytesUntil.
#define FILE_WRITE "w"
#define FILE_READ  "r"
struct File {
  operator bool() const { return true; }
  void println(const char*) {}
  void println() {}
  int printf(const char*, ...) { return 0; }
  void flush() {}
  void close() {}
  int available() { return 0; }
  int read(uint8_t*, size_t) { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t*, size_t n) { return n; }
  int readBytesUntil(char, char*, size_t) { return 0; }
  // Directory walking, first needed by log_wipe().
  File openNextFile() { return File(); }
  const char *name() { return ""; }
};
struct SDClass {
  bool begin(int, SPIClass&, uint32_t) { return false; }
  bool exists(const char*) { return false; }
  bool mkdir(const char*) { return true; }
  bool remove(const char*) { return true; }
  File open(const char*, const char*) { return File(); }
  File open(const char*) { return File(); }
};
static SDClass SD;
