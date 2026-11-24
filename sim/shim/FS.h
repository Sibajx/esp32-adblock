// Simulador: LittleFS sobre una carpeta, con el tamaño real de la partición (2.44 MB) y bloques de
// 4 KB, para que una lista demasiado grande falle igual que en el ESP32.
#pragma once
#include <cstdio>
#include <memory>
#include <string>

#include "Arduino.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {
class File {
 public:
  File() = default;
  File(std::shared_ptr<FILE> f, std::string path) : f_(std::move(f)), path_(std::move(path)) {}
  explicit operator bool() const { return f_ != nullptr; }
  size_t read(uint8_t* buf, size_t n);
  int read();
  size_t write(const uint8_t* buf, size_t n);
  bool seek(size_t pos);
  size_t size() const;
  int available();
  void close() { f_.reset(); }

 private:
  std::shared_ptr<FILE> f_;
  std::string path_;
};

class FS {
 public:
  bool begin(bool formatOnFail = false);
  File open(const char* path, const char* mode);
  File open(const String& path, const char* mode) { return open(path.c_str(), mode); }
  bool remove(const char* path);
  bool exists(const char* path) const;
  bool exists(const String& path) const { return exists(path.c_str()); }
  bool remove(const String& path) { return remove(path.c_str()); }
  bool rename(const char* from, const char* to);
  bool mkdir(const char* path);
  size_t totalBytes() const { return 0x270000; }  // igual que partitions.csv
  size_t usedBytes() const;
  std::string real(const char* path) const;

 private:
  std::string root_;
};
}  // namespace fs
