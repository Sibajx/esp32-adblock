#pragma once
#include <cstdint>
struct ip_addr_t { uint8_t b[4]; };
#define IP_ADDR4(a, x0, x1, x2, x3) do { (a)->b[0] = x0; (a)->b[1] = x1; (a)->b[2] = x2; (a)->b[3] = x3; } while (0)
inline void dns_setserver(int, const ip_addr_t*) {}
