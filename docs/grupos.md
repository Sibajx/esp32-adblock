# Grupos de los horarios: «Redes sociales» y «Juegos»

Los horarios del panel (sección "Horarios") cortan uno o más grupos en ciertos aparatos y a ciertas
horas. «Todo internet» corta todo. Los otros dos grupos son listas fijas que vienen dentro del
firmware, en `lib/siblock_core/family.cpp` (`kSocial` y `kGames`).

Las listas apuntan a **servicios**, no a cada dominio que existe: se pone el dominio base
(por ejemplo `tiktok.com`) y con eso se cortan también todos sus subdominios
(`v16-webapp.tiktok.com`, `www.tiktok.com`…). Un nombre que solo *termina parecido*
(`ax.com`, `instagram.com.evil.net`) no cuenta.

## Qué corta cada grupo

**Redes sociales** (74 dominios): Facebook, Instagram, Threads, TikTok, Snapchat, X (Twitter),
Bluesky, Pinterest, Reddit, Discord y Twitch.

**Juegos** (91 dominios): Roblox, Minecraft (Mojang), Fortnite y Epic Games, Steam, Xbox,
PlayStation, Nintendo, EA, Activision y Blizzard (Call of Duty, Battle.net), Riot (League of
Legends, Valorant), Supercell (Brawl Stars, Clash of Clans, Clash Royale), Garena (Free Fire),
HoYoverse (Genshin), Niantic (Pokémon GO), Among Us, Stumble Guys, Mobile Legends, CubeCraft y
Easy Anti-Cheat.

## De dónde salen

- **AdGuard HostlistsRegistry**, la parte de "servicios bloqueados":
  <https://github.com/AdguardTeam/HostlistsRegistry>. Licencia **GPL-3.0**, la misma de SiBlock.
  Se tomó el commit `ae662d803ad6bfb4b9de09661f9396c978952dac` de `main` (2026-11-22) y de cada
  servicio solo los dominios base.
- **Agregados a mano** (no están en AdGuard): `threads.com`, `threads.net`, `fortnite.com`,
  `xbox.com`, y los dominios de Supercell, Garena / Free Fire, HoYoverse, Niantic, Innersloth
  (Among Us), Stumble Guys y Moonton (Mobile Legends).
- **Falta verificar con un aparato real:** `ggblueshark.com` y `ggwhitehawk.com` (Free Fire),
  `stumbleguys.com` y `kitkagames.com` (Stumble Guys). Puede que el juego use otros nombres.

Para actualizar las listas: copiar los dominios nuevos en `family.cpp` **en orden de bytes**
(`LC_ALL=C sort`), porque se buscan con búsqueda binaria. La prueba `make -C test_host test`
revisa que sigan ordenadas y cuántas hay; si cambia el número, hay que cambiarlo también en la
prueba.

## Lo que se deja fuera a propósito

- **Nada que comparta media casa:** nunca `microsoft.com`, `live.com`, `login.live.com`,
  `google.com`, `googleapis.com`, `gstatic.com`, `apple.com` ni CDNs compartidos (Akamai,
  CloudFront, `s3.amazonaws.com` completo). Si entraran, a la hora del horario se caerían el
  correo, la escuela en línea, las actualizaciones y las cuentas. Solo hay un nombre de Amazon S3,
  el de Twitter (`impala-media-production.s3.amazonaws.com`), y es exacto.
- **WhatsApp y Messenger** no están: son mensajes, no redes. Así el niño puede avisar a casa.
- **YouTube** no está en «Redes sociales» porque se usa para la escuela. Si hace falta, puede ser
  un grupo aparte más adelante, con `googlevideo.com`, `youtu.be`, `youtube-nocookie.com`,
  `youtube.com`, `youtubei.googleapis.com`, `youtubekids.com` y `ytimg.com`. Hoy **no** se corta.

## Lo que conviene saber (efectos de lado)

Mientras el horario está activo en ese aparato:

- **Minecraft Education** también deja de entrar (usa los mismos nombres de Minecraft y de Xbox).
- **La cuenta de Xbox** no inicia sesión, ni en la consola ni en juegos de PC que la piden.
- **La Nintendo Switch** dice «sin internet» o que no pudo conectarse; no es falla del WiFi.
- **Juegos con Easy Anti-Cheat** (Fortnite y muchos de PC) no abren, aunque el juego en sí no
  esté en la lista.
- **«Entrar con Facebook»** en otras páginas o apps no funciona.
- **Los enlaces de X** (los que empiezan con `t.co`) no abren, aunque lleven a otra página.

Fuera del horario todo vuelve a funcionar solo. Como siempre, es una ayuda y no un candado: un
aparato que cambia su dirección en el WiFi, usa DNS cifrado o una VPN se lo salta.
