#pragma once
struct MDNSResponder {
  bool begin(const char*) { return true; }
  void addService(const char*, const char*, int) {}
};
inline MDNSResponder MDNS;
