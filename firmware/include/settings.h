#pragma once

#ifdef FW_VERSION
#undef FW_VERSION
#endif
#define FW_VERSION        "1.10.0"

#ifndef HOSTNAME
#define HOSTNAME          "nowplaying"
#endif
#ifndef ROTATION
#define ROTATION          1
#endif
#ifndef IDLE_TIMEOUT_MS
#define IDLE_TIMEOUT_MS   15000
#endif
#ifndef PAUSE_TO_IDLE_MS
#define PAUSE_TO_IDLE_MS  30000
#endif
#ifndef COVER_MAX_BYTES
#define COVER_MAX_BYTES   12288
#endif
#ifndef ICON_MAX_BYTES
#define ICON_MAX_BYTES    4096
#endif
#ifndef BIG_MAX_BYTES
#define BIG_MAX_BYTES     20480
#endif
