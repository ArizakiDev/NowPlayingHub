#pragma once
// Reglages du projet (versionnes dans Git). Les secrets (Wi-Fi, jeton) sont dans config.h, qui n'est JAMAIS commite.

// La version vit ici (et pas dans config.h) : on la monte avant chaque release.
#ifdef FW_VERSION
#undef FW_VERSION
#endif
#define FW_VERSION        "1.8.0"

#ifndef HOSTNAME
#define HOSTNAME          "nowplaying"   // http://nowplaying.local
#endif
#ifndef ROTATION
#define ROTATION          1              // 1 = paysage ; 3 = paysage a l'envers
#endif
#ifndef IDLE_TIMEOUT_MS
#define IDLE_TIMEOUT_MS   15000          // sans nouvelles du PC -> ecran d'attente
#endif
#ifndef PAUSE_TO_IDLE_MS
#define PAUSE_TO_IDLE_MS  30000          // apres X ms en pause -> ecran idle
#endif
#ifndef COVER_MAX_BYTES
#define COVER_MAX_BYTES   12288          // v1.8 : 16384 -> 12288 (le PC compresse a <= 11000 o)
#endif
#ifndef ICON_MAX_BYTES
#define ICON_MAX_BYTES    4096
#endif
#ifndef BIG_MAX_BYTES
#define BIG_MAX_BYTES     20480          // pochette plein ecran 240x240 (envoyee a la demande)
#endif
