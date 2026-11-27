#!/usr/bin/env python3
"""Le pasa el WiFi a SiBlock por USB (protocolo Improv Serial).

Uso: python3 tools/improv_wifi.py "NombreDeRed" [/dev/ttyUSB0]
La contraseña se pide sin mostrarla en pantalla y no se guarda en ningún lado.
"""
import getpass
import sys
import time

import serial

STATES = {2: "esperando datos", 3: "conectando...", 4: "¡conectada!"}
ERRORS = {1: "paquete inválido", 2: "comando desconocido", 3: "no se pudo conectar (¿contraseña?)", 0xFF: "error desconocido"}


def packet(ptype: int, data: bytes) -> bytes:
    body = b"IMPROV" + bytes([1, ptype, len(data)]) + data
    return body + bytes([sum(body) & 0xFF])


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ssid = sys.argv[1].encode()
    port = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyUSB0"
    pwd = getpass.getpass(f"Contraseña de {sys.argv[1]}: ").encode()

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = s.rts = False  # abrir el puerto sin reiniciar la placa
    s.open()
    # En placas con CH340, abrir el puerto puede reiniciarlas: se espera a que arranque.
    time.sleep(3)
    s.reset_input_buffer()

    payload = bytes([len(ssid)]) + ssid + bytes([len(pwd)]) + pwd
    s.write(packet(0x03, bytes([0x01, len(payload)]) + payload))
    print("Enviado. Esperando respuesta de la placa (puede tardar unos minutos: primero baja la lista)...")

    buf = b""
    end = time.time() + 600  # v0.2.1 no contesta hasta terminar la lista
    text = b""
    while time.time() < end:
        chunk = s.read(256)
        buf += chunk
        text += chunk
        while b"\n" in text:  # lo que la placa escribe en su registro, para ver qué pasa
            line, text = text.split(b"\n", 1)
            line = line.strip()
            if line and b"IMPROV" not in line and not line.startswith(b"STATS"):
                print("  placa>", line.decode(errors="replace"), flush=True)
        while (i := buf.find(b"IMPROV")) >= 0 and len(buf) >= i + 9:
            ptype, ln = buf[i + 7], buf[i + 8]
            if len(buf) < i + 10 + ln:
                break
            data = buf[i + 9 : i + 9 + ln]
            buf = buf[i + 10 + ln :]
            if ptype == 0x01:
                print("Estado:", STATES.get(data[0], data[0]), flush=True)
            elif ptype == 0x02 and data[0]:
                sys.exit("Error: " + ERRORS.get(data[0], str(data[0])))
            elif ptype == 0x04:
                url = data[3 : 3 + data[2]].decode(errors="replace") if len(data) > 3 else ""
                print("Listo." + (f" Panel: {url}" if url else ""))
                return
        if len(buf) > 4096:
            buf = buf[-64:]
    sys.exit("La placa no contestó a tiempo. Revisa el cable y vuelve a intentar.")


if __name__ == "__main__":
    main()
