// =============================================================================
//  Utf8Ascii.h — convertit l'UTF-8 (accents, guillemets typographiques...) en ASCII
//  affichable par les polices TFT_eSPI (qui n'ont pas les glyphes accentues).
//  - cleanUTF8Into() : version buffer (aucune allocation)
//  - cleanUTF8()     : version String, a utiliser a la RECEPTION des donnees
//                      (jamais dans la boucle d'affichage)
//  Gere aussi le "mojibake" (UTF-8 relu en Latin-1 : "Ã©" -> "e").
// =============================================================================
#pragma once
#include <Arduino.h>

namespace u8 {
// U+00C0..U+00FF  (Æ, ß traites a part)
static const char LAT1[] =
  "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"
  "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
// U+0100..U+017F  (Œ/œ traites a part)
static const char EXTA[] =
  "AaAaAaCcCcCcCcDd" "DdEeEeEeEeEeGgGg" "GgGgHhHhIiIiIiIi" "IiIiJjKkkLlLlLlL"
  "lLlNnNnNnnNnOoOo" "OoOoRrRrRrSsSsSs" "SsTtTtTtUuUuUuUu" "UuUuWwYyYZzZzZzs";
}

inline size_t cleanUTF8Into(const char* in, char* out, size_t cap) {
  if (!out || !cap) return 0;
  size_t o = 0;
  auto put = [&](const char* s) {
    for (; *s && o + 1 < cap; s++) {
      if (*s == ' ' && (o == 0 || out[o - 1] == ' ')) continue;   // pas d'espaces doubles/en tete
      out[o++] = *s;
    }
  };
  const uint8_t* p = (const uint8_t*)in;
  bool prevA = false;                                   // "Ã" en attente (mojibake)
  while (p && *p) {
    uint32_t c = *p; int extra = 0;
    if (c < 0x80)                  extra = 0;
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; extra = 3; }
    else { p++; continue; }                             // octet isole invalide
    p++;
    for (int i = 0; i < extra; i++) {
      if ((*p & 0xC0) != 0x80) { c = 0xFFFD; break; }
      c = (c << 6) | (*p & 0x3F); p++;
    }
    if (prevA) {                                        // "Ã" + "©" = "é" mal decode
      prevA = false;
      if (c >= 0x80 && c <= 0xBF) c = 0xC0 | (c & 0x3F); else put("A");
    } else if (c == 0xC3) { prevA = true; continue; }

    char b[2] = {0, 0};
    if (c < 0x20)                       put(" ");
    else if (c < 0x7F)                { b[0] = (char)c; put(b); }
    else if (c == 0xA0)                 put(" ");
    else if (c == 0xC6)                 put("AE");
    else if (c == 0xE6)                 put("ae");
    else if (c == 0xDF)                 put("ss");
    else if (c >= 0xC0 && c <= 0xFF)  { b[0] = u8::LAT1[c - 0xC0]; put(b); }
    else if (c == 0x152)                put("OE");
    else if (c == 0x153)                put("oe");
    else if (c >= 0x100 && c <= 0x17F){ b[0] = u8::EXTA[c - 0x100]; put(b); }
    else switch (c) {
      case 0xAB: case 0xBB: case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033: put("\""); break;
      case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032:                      put("'");  break;
      case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: put("-"); break;
      case 0x2026: put("..."); break;
      case 0x2022: case 0xB7: put("-"); break;
      case 0xB0:   put("o");   break;
      case 0x20AC: put("EUR"); break;
      case 0xA3:   put("GBP"); break;
      case 0xA9:   put("(c)"); break;
      case 0x2122: put("TM");  break;
      default: break;                                    // emoji, symboles : ignores
    }
  }
  if (prevA) put("A");
  while (o > 0 && out[o - 1] == ' ') o--;                // pas d'espace final
  out[o] = 0;
  return o;
}

inline String cleanUTF8(const String& str) {
  const size_t cap = (size_t)str.length() * 2 + 4;
  char* buf = (char*)malloc(cap);
  if (!buf) return str;
  cleanUTF8Into(str.c_str(), buf, cap);
  String r(buf);
  free(buf);
  return r;
}
