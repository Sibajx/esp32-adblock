// SiBlock: catálogo de listas de bloqueo y la regla para aceptar una lista nueva.
// Sin dependencias de Arduino.
#pragma once
#include <cstddef>
#include <cstdint>

namespace siblock {

// La general va siempre; estafas y español son opcionales y vienen apagadas de fábrica.
constexpr uint8_t kListGeneral = 1, kListScam = 2, kListSpanish = 4, kListsDefault = kListGeneral;
bool validListMask(uint32_t mask);  // impar (incluye la general) y <= 7

// URLs a bajar, en orden: estafas, español, general (2). Así, si se llega al tope de dominios, lo
// que queda fuera son anuncios, nunca estafas. Máscara inválida -> solo la general.
// Una lista sin URL (nullptr en el catálogo) no se ofrece ni se baja.
size_t listUrls(uint8_t mask, const char** out, size_t max);
// Listas que el firmware puede ofrecer (bits de las que tienen URL).
uint8_t listsOffered();
constexpr size_t kMaxListUrls = 4;

// ¿Se acepta una lista nueva de `total` dominios si la actual tiene `current`? Una mucho más chica
// suele ser una descarga rota: se rechaza si current > 1000 y total < mínimo (la mitad, o el 90 %
// si alguna lista llegó sin tamaño), salvo que sea igual (±1 %) a la última rechazada (entonces
// es a propósito). allowShrink (cambió la selección de listas) acepta siempre.
bool acceptListSize(uint32_t total, uint32_t current, bool sizeUnknown, uint32_t rejectedTotal, bool allowShrink);

}  // namespace siblock
