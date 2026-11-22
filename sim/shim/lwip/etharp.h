// Simulador: lo justo de lwIP para preguntar la tabla ARP (src/family_store.cpp).
// La IP 127.0.0.x tiene la MAC 02:00:00:00:00:<x>, salvo que $SIM_FLASH/.arp (se relee en cada
// consulta) diga otra cosa con una línea "127.0.0.x 02:aa:bb:cc:dd:ee" (otra MAC) o
// "127.0.0.x -" (no está en la tabla). En la placa: el etharp.h de verdad.
#pragma once
#include <cstdint>
#include <sys/types.h>

typedef int8_t err_t;
#define ERR_OK 0
#define ERR_VAL -6
struct ip4_addr_t {
  uint32_t addr;  // orden de red, como uint32_t(IPAddress)
};
struct eth_addr {
  uint8_t addr[6];
};
struct netif {};
extern struct netif* netif_default;
ssize_t etharp_find_addr(struct netif* netif, const ip4_addr_t* ipaddr, struct eth_addr** eth_ret,
                         const ip4_addr_t** ip_ret);

// tcpip_api_call corre la función en el hilo de lwIP; aquí, en el mismo hilo.
struct tcpip_api_call_data {
  err_t err;
};
typedef err_t (*tcpip_api_call_fn)(struct tcpip_api_call_data* call);
inline err_t tcpip_api_call(tcpip_api_call_fn fn, struct tcpip_api_call_data* call) { return fn(call); }
