// ============================================================================
//  User_Setup.h : TFT_eSPI pour ESP32-2432S028R (Cheap Yellow Display, ILI9341)
//  PlatformIO : injecte automatiquement via platformio.ini.
//  Arduino IDE : copie ce fichier dans libraries/TFT_eSPI/User_Setup.h
// ============================================================================
#pragma once

#define ILI9341_2_DRIVER      // variante ILI9341 de la plupart des CYD
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Couleurs inversees (fond blanc au lieu de noir) -> decommente :
// #define TFT_INVERSION_ON
// Rouge/bleu inverses -> bascule TFT_BGR <-> TFT_RGB :
#define TFT_RGB_ORDER TFT_BGR

// --- Pins ecran (bus HSPI) ---
#define USE_HSPI_PORT
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// Le tactile XPT2046 (CS 33, IRQ 36, CLK 25, MOSI 32, MISO 39) est sur un bus
// SPI separe : il est gere dans main.cpp avec la lib XPT2046_Touchscreen.
// (donc volontairement PAS de TOUCH_CS ici)

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_GFXFF            // polices FreeSans/FreeSansBold (dashboard v1.7)

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
