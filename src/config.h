// Ajustes de SiBlock. El WiFi se da desde la página de instalación (por USB) o desde la red
// SiBlock-XXXX; la contraseña del panel se crea en el panel. Todo se guarda en la placa (NVS);
// secrets.h es opcional (solo para quien compila).
#pragma once

// Versión: CI la inyecta con -DSIBLOCK_VERSION="vX.Y.Z".
#ifndef SIBLOCK_VERSION
#define SIBLOCK_VERSION "dev"
#endif

// Listas de bloqueo: el catálogo (general, estafas, español) y sus URLs están en
// lib/siblock_core/lists.cpp. La general va siempre; las otras se eligen en el panel.

// Tope de dominios (contando repetidos entre listas): 180k * 5 bytes = 900 KB en flash.
// Al actualizar conviven la lista vieja y la nueva, así que no subirlo de ~200k.
#define MAX_DOMAINS 180000

// Cada cuántas horas se vuelve a bajar la lista.
#define REFRESH_HOURS 24

// DNS reales a los que se reenvía lo que no está bloqueado.
#define UPSTREAM_DNS_1 IPAddress(1, 1, 1, 1)
#define UPSTREAM_DNS_2 IPAddress(9, 9, 9, 9)

// Anti "DNS rebinding": si un dominio de internet responde con una IP de la casa (192.168.x.x,
// 10.x...), se rechaza. Protege al panel y a otros aparatos de páginas maliciosas. Si algún
// servicio legítimo lo necesita (p. ej. plex.direct), agrégalo en "Permitir" desde el panel.
#define REBIND_PROTECTION 1

// Nombre en la red (http://siblock.local si tu red soporta mDNS).
#define DEVICE_HOSTNAME "siblock"
// Nombre de antes: el panel lo sigue aceptando si llega con ese nombre, pero la placa ya no lo
// anuncia por mDNS (solo "siblock.local"). "adblock.local" solo funciona si tu router o el
// archivo hosts lo resuelven; si no, usa http://siblock.local o la IP.
#define LEGACY_HOSTNAME "adblock"
// El panel solo abre por su IP, "siblock", "siblock.local", "adblock" o "adblock.local"
// (anti DNS-rebinding). Si tu router le da otro nombre (p. ej. "siblock.lan"), ponlo aquí;
// "" = ninguno más.
#define PANEL_EXTRA_HOST ""

// IP fija. Mejor: reservar la IP en el router (DHCP) y dejar esto en 0.
#define USE_STATIC_IP 0
#define STATIC_IP IPAddress(192, 168, 1, 53)
#define STATIC_GATEWAY IPAddress(192, 168, 1, 1)
#define STATIC_SUBNET IPAddress(255, 255, 255, 0)

// Dominios que nunca se bloquean (también sus subdominios).
static const char* const ALLOWLIST[] = {
    "s.youtube.com",  // historial de YouTube
    "clients4.google.com",
};

// Memoria de configuración (NVS). "Borrar la configuración" (BOOT 10 s) borra este espacio entero.
#define NVS_NAMESPACE "siblock"
#define NVS_WIFI_SSID "wifi_ssid"
#define NVS_WIFI_PASS "wifi_pass"
#define NVS_PANEL_HASH "panel_hash"  // 32 B
#define NVS_PANEL_SALT "panel_salt"  // 16 B
#define NVS_PANEL_ITER "panel_iter"  // entero
#define NVS_AP_PASS "ap_pass"
// Opción "Apagar iCloud Private Relay en esta casa": u32 0/1; si falta, 0 (apagada de fábrica).
#define NVS_RELAY_BLOCK "relay_block"
// Listas elegidas: u32 con bits 1 = general (siempre), 2 = estafas, 4 = anuncios en español
// (lib/siblock_core/lists.h). Si falta o no es válida: solo la general (de fábrica).
#define NVS_LISTS "lists"
// Listas con las que se armó la lista guardada: si cambia la selección, la siguiente lista puede
// ser más chica sin que se rechace.
#define NVS_LISTS_BUILT "lists_built"

// Nube (siblock.s-dev.win). Todo apagado de fábrica: sin estas opciones encendidas en el panel,
// la placa solo se conecta a internet para reenviar el DNS, bajar la lista y pedir la hora.
#define SIBLOCK_CLOUD_BASE "https://siblock.s-dev.win"
// Número de instalación: 32 hex al azar (no dice quién eres). "Borrar la configuración" lo cambia.
#define NVS_INSTALL_ID "install_id"
// "Compartir datos anónimos": u32; solo 1 cuenta como encendida (falta, 2 o ilegible = apagada).
#define NVS_CLOUD_SHARE "cloud_share"
// "Avisarme de versiones nuevas": u32; solo 1 cuenta como encendida.
#define NVS_VER_CHECK "ver_check"
// "Usar los permitidos de la comunidad": u32; solo 1 cuenta como encendida. Con ella, la placa baja
// una vez al día SIBLOCK_CLOUD_BASE "/api/v1/permitidos" (no manda nada) y la guarda en
// /comunidad.bin. Apagarla o "borrar la configuración" borra esa lista.
#define NVS_COMMUNITY "comunidad"

// Avisos por Telegram al dueño, con SU propio bot (apagados de fábrica). Mandan textos cortos con
// números y estados (nunca IPs, MACs, nombres de aparatos ni sitios visitados) a
// SIBLOCK_TELEGRAM_BASE "/bot<token>/sendMessage". El token no sale nunca por el panel, la serie ni
// el respaldo; el chat solo lo devuelve GET /api/telegram (con sesión) y no va en el respaldo.
// "Borrar la configuración" los borra.
#define SIBLOCK_TELEGRAM_BASE "https://api.telegram.org"
// Token del bot (texto, lo da @BotFather). Solo se escribe desde el panel; ninguna ruta lo devuelve.
#define NVS_TG_TOKEN "tg_token"
// Número de chat al que se manda (texto, puede ser negativo en grupos).
#define NVS_TG_CHAT "tg_chat"
// "Mandarme avisos por Telegram": u32; solo 1 cuenta como encendido (falta, 2 o ilegible = apagado).
#define NVS_TG_ON "tg_on"
// Qué avisos: u32 con bits 1 = la placa se reinició, 2 = la lista no se pudo actualizar,
// 4 = resumen semanal. Si falta: los tres.
#define NVS_TG_EVENTS "tg_ev"
// Día (días desde 1970, en la hora de la casa) del último domingo con resumen: no se manda dos veces.
#define NVS_TG_WEEK "tg_week"
// Versión con la que arrancó la vez anterior (solo con los avisos encendidos): si cambió, el
// aviso de reinicio dice «actualización».
#define NVS_TG_FW "tg_fw"
