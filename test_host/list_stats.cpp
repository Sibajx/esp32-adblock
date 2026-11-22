// Mide una lista real en la PC:  curl -s URL | ./list_stats
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "../lib/adblock_core/adblock_core.h"

int main() {
  using namespace adblock;
  std::vector<uint64_t> hs;
  std::string line, d;
  size_t lines = 0;
  while (std::getline(std::cin, line)) {
    lines++;
    if (parseListLine(line.data(), line.size(), d)) hs.push_back(hashDomain(d));
  }
  std::sort(hs.begin(), hs.end());
  size_t before = hs.size();
  hs.erase(std::unique(hs.begin(), hs.end()), hs.end());
  std::vector<uint32_t> idx(kIndexSize);
  buildIndex(hs.data(), hs.size(), idx.data());
  uint32_t maxB = 0;
  for (size_t b = 0; b + 1 < kIndexSize; b++) maxB = std::max(maxB, idx[b + 1] - idx[b]);
  std::printf("lineas=%zu dominios=%zu unicos=%zu bytes_lista=%zu cubeta_max=%u\n", lines, before,
              hs.size(), hs.size() * 8, maxB);
}
