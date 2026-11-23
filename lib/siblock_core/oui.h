// SiBlock: marca sugerida de un aparato por el prefijo de su MAC (OUI, lista MA-L del IEEE).
// Es solo una sugerencia para el nombre: nunca se aplica sola y puede fallar (los módems del
// proveedor son Huawei, ZTE o TP-Link; algunos aparatos usan piezas de otra marca).
//
// La tabla está en la flash (oui_table.h, generada con tools/gen_oui.py). No usa RAM dinámica.
// La MAC solo se lee: no se guarda ni se copia.
#pragma once
#include <cstdint>

namespace siblock {

// Códigos de una palabra para guardar la sugerencia en RAM sin la MAC.
constexpr uint8_t kOuiNone = 0;       // marca que no conocemos
constexpr uint8_t kOuiPrivate = 255;  // dirección privada (MAC inventada por el aparato) o de grupo

// ¿La MAC es "administrada localmente" (bit 0x02 del primer byte: la dirección privada que ponen
// los celulares con iOS 14+ o Android 10+) o de grupo (bit 0x01)? Esas no dicen la marca.
bool macIsPrivate(const uint8_t mac[6]);

uint8_t ouiCode(const uint8_t mac[6]);  // kOuiNone, kOuiPrivate o 1..N
const char* ouiLabel(uint8_t code);     // etiqueta en español, o nullptr con kOuiNone/kOuiPrivate/fuera de rango

// Etiqueta de la marca ("Samsung", "Consola PlayStation"), o nullptr si no se conoce o es privada.
const char* ouiBrand(const uint8_t mac[6]);

}  // namespace siblock
