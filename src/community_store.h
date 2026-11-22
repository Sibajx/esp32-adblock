// Permitidos de la comunidad en la placa: los baja, los guarda en /comunidad.bin y se los pasa a
// BlockList. Solo con «Usar los permitidos de la comunidad» encendido (apagado de fábrica).
// Van aparte de las reglas propias: no salen en «Mis reglas» ni en el respaldo.
#pragma once
#include <Arduino.h>

class BlockList;

namespace community {

// Con la opción encendida lee /comunidad.bin (si falta o está roto: sin permitidos hasta la
// próxima bajada); apagada, lo borra.
void begin(BlockList* list);
// Baja la lista, la revisa (todo o nada), la guarda y la aplica. Si algo falla queda la de antes.
bool refresh();
// Borra la lista de la RAM y de la flash (al apagar la opción y al borrar la configuración).
void clear();

struct Info {
  uint16_t count = 0;    // sitios en uso
  uint32_t version = 0;  // número de la lista que mandó el Worker
  uint32_t updated = 0;  // segundos desde 1970 de la última bajada buena (0 = nunca o sin hora)
};
Info info();

}  // namespace community
