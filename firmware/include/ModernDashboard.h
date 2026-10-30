// =============================================================================
//  ModernDashboard.h — écran idle "widgets iOS / StandBy" pour CYD 320x240
//  Dépendances : TFT_eSPI >= 2.5 (fillSmoothCircle, drawWideLine) + LOAD_GFXFF
//  Polices     : FreeSans / FreeSansBold (ASCII seulement -> pas d'accents)
//  RAM         : 2 sprites 16 bits = ~28 Ko (ticker 272x22 + heure 135x64)
//  Aucun String dynamique : uniquement des buffers char[] statiques.
// =============================================================================
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>

namespace ui {

// ---------- Palette (RGB565) -------------------------------------------------
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t C_BG    = 0x0000;                     // noir pur #000000
constexpr uint16_t C_CARD  = rgb565(0x1C, 0x1C, 0x1E);  // cartes
constexpr uint16_t C_CARD2 = rgb565(0x2C, 0x2C, 0x2E);  // carte mise en avant
constexpr uint16_t C_TRACK = rgb565(0x3A, 0x3A, 0x3C);  // séparateurs / pistes
constexpr uint16_t C_TEXT  = 0xFFFF;                    // blanc pur
constexpr uint16_t C_MUTED = rgb565(0x8E, 0x8E, 0x93);  // sous-titres
constexpr uint16_t C_SOFT  = rgb565(0xAE, 0xAE, 0xB2);  // gris clair
constexpr uint16_t C_BLUE  = rgb565(0x0A, 0x84, 0xFF);  // accent bleu
constexpr uint16_t C_AMBER = rgb565(0xFF, 0x9F, 0x0A);  // accent ambre

// ---------- Géométrie --------------------------------------------------------
constexpr int16_t R_CARD = 12;
// Ticker (aligné sur les cartes)
constexpr int16_t TK_X = 10, TK_Y = 6, TK_W = 300, TK_H = 28;
constexpr int16_t TK_SPR_X = 28, TK_SPR_Y = 9, TK_SPR_W = 272, TK_SPR_H = 22;
constexpr int16_t TK_GAP = 48;          // espace entre deux répétitions
constexpr uint16_t TK_STEP_MS = 30;     // 1 px / 30 ms
constexpr uint16_t TK_PAUSE_MS = 1800;  // pause au début de chaque cycle
// Cartes
constexpr int16_t CARD_Y = 42, CARD_W = 145, CARD_H = 188;
constexpr int16_t TIME_X = 10, WX_X = 165;
// Sprite heure : centré verticalement dans la carte
constexpr int16_t TS_W = 135, TS_H = 64;
constexpr int16_t TS_X = TIME_X + 5, TS_Y = CARD_Y + 48;
constexpr int16_t DATE_CY = TS_Y + TS_H + 18;

// ---------- Données d'entrée -------------------------------------------------
enum WxIcon : uint8_t { WX_SUN, WX_PARTLY, WX_CLOUD, WX_RAIN, WX_SNOW, WX_STORM, WX_FOG };

struct WeatherData {
  bool    valid = false;
  int8_t  tempC = 0, feelsC = 0;
  uint8_t windKmh = 0, humidity = 0;
  WxIcon  icon = WX_CLOUD;
  char    label[20] = {0};   // ex: "Ciel degage" (ASCII)
};

struct DashboardData {
  uint8_t hour = 0, minute = 0, second = 0;
  uint8_t wday = 0;          // 0 = dimanche (tm_wday)
  uint8_t mday = 1;          // 1..31
  uint8_t month = 1;         // 1..12 (tm_mon + 1)
  bool    timeValid = true;  // false tant que le NTP n'est pas synchronise -> "--:--"
  WeatherData wx;
  const char* ticker = nullptr;   // texte du bandeau (copié en interne)
};

// Convertit un code météo WMO (Open-Meteo) en icône
inline WxIcon wmoToIcon(int c) {
  if (c == 0) return WX_SUN;
  if (c <= 2) return WX_PARTLY;
  if (c == 3) return WX_CLOUD;
  if (c == 45 || c == 48) return WX_FOG;
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return WX_RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_SNOW;
  if (c >= 95) return WX_STORM;
  return WX_CLOUD;
}

// ---------- Briques graphiques réutilisables (échelle k : 1.0 = ~50 px) ------
inline void degreeRing(TFT_eSPI& t, int16_t x, int16_t y, int16_t r, uint16_t fg, uint16_t bg, int16_t th = 2) {
  t.fillSmoothCircle(x, y, r, fg, bg);
  t.fillSmoothCircle(x, y, r - th, bg, fg);
}

inline void drawWxIcon(TFT_eSPI& t, int16_t cx, int16_t cy, WxIcon ic, float k, uint16_t bg) {
  auto S = [k](int v) -> int16_t { return (int16_t)lroundf(v * k); };
  auto R = [k](int v) -> int16_t { int16_t r = (int16_t)lroundf(v * k); return r < 1 ? 1 : r; };
  auto W = [k](float w) -> float { w *= k; return w < 1.5f ? 1.5f : w; };
  auto sun = [&](int16_t x, int16_t y, int r, int ri, int ro, float wd) {
    t.fillSmoothCircle(x, y, R(r), C_AMBER, bg);
    for (uint8_t i = 0; i < 8; i++) {
      const float a = i * 0.785398f, ca = cosf(a), sa = sinf(a);
      t.drawWideLine(x + ca * S(ri), y + sa * S(ri), x + ca * S(ro), y + sa * S(ro), W(wd), C_AMBER, bg);
    }
  };
  auto cloud = [&](int16_t x, int16_t y, uint16_t col) {
    t.fillSmoothCircle(x + S(-12), y + S(2),  R(9),  col, bg);
    t.fillSmoothCircle(x + S(1),   y + S(-5), R(12), col, bg);
    t.fillSmoothCircle(x + S(13),  y + S(3),  R(8),  col, bg);
    t.fillRect(x + S(-12), y + S(2), S(25), S(9), col);
  };
  switch (ic) {
    case WX_SUN:    sun(cx, cy, 11, 16, 23, 3); break;
    case WX_PARTLY: sun(cx + S(8), cy + S(-8), 8, 12, 18, 2.5f); cloud(cx + S(-2), cy + S(6), C_SOFT); break;
    case WX_CLOUD:  cloud(cx, cy, C_SOFT); break;
    case WX_RAIN:
      cloud(cx, cy + S(-6), C_SOFT);
      for (uint8_t i = 0; i < 3; i++)
        t.drawWideLine(cx + S(-10 + i * 10), cy + S(10), cx + S(-13 + i * 10), cy + S(21), W(2.5f), C_BLUE, bg);
      break;
    case WX_SNOW:
      cloud(cx, cy + S(-6), C_SOFT);
      for (uint8_t i = 0; i < 3; i++)
        t.fillSmoothCircle(cx + S(-10 + i * 10), cy + S(13 + (i & 1) * 6), R(2), C_TEXT, bg);
      break;
    case WX_STORM:
      cloud(cx, cy + S(-8), C_MUTED);
      t.fillTriangle(cx + S(4), cy, cx + S(-5), cy + S(12), cx + S(1), cy + S(12), C_AMBER);
      t.fillTriangle(cx + S(-1), cy + S(9), cx + S(7), cy + S(9), cx + S(-3), cy + S(22), C_AMBER);
      break;
    case WX_FOG:
      for (uint8_t i = 0; i < 3; i++)
        t.drawWideLine(cx + S(-18), cy + S(-10 + i * 10), cx + S(18 - i * 8), cy + S(-10 + i * 10), W(3), C_SOFT, bg);
      break;
  }
}

// =============================================================================
class Dashboard {
 public:
  explicit Dashboard(TFT_eSPI& t) : tft(t), tkSpr(&t), timeSpr(&t) {}

  // À appeler une fois après tft.init()/setRotation(1). false si RAM insuffisante.
  bool begin() {
    if (ready) return true;                       // idempotent (v1.8)
    tkSpr.setColorDepth(16);
    timeSpr.setColorDepth(16);
    ready = tkSpr.createSprite(TK_SPR_W, TK_SPR_H) &&
            timeSpr.createSprite(TS_W, TS_H);
    invalidate();
    return ready;
  }

  // v1.8 : libere les sprites (~29 Ko) quand l'accueil n'est pas affiche ; begin() les recree
  void end() { if (ready) { tkSpr.deleteSprite(); timeSpr.deleteSprite(); ready = false; } }
  bool isReady() const { return ready; }

  // Force un redessin complet au prochain update() (retour d'un autre écran)
  void invalidate() {
    needFrame = true;
    lastSec = 0xFF; lastColon = 2; lastDateKey = 0xFFFFFFFFu;
    wxDrawn = false; tkBuf[0] = '\x01';  // valeur impossible -> force reset ticker
  }

  // Routine de rendu : appeler à chaque tour de loop() (coût quasi nul si rien ne change)
  void update(const DashboardData& d, uint32_t now) {
    if (!ready) return;
    if (needFrame) { drawFrame(); needFrame = false; }

    const uint8_t colon = ((now / 500) & 1) == 0;
    const uint8_t sk = d.timeValid ? d.second : 60;
    if (sk != lastSec || colon != lastColon) {
      renderTime(d, colon);
      lastSec = sk; lastColon = colon;
    }

    const uint32_t dk = d.timeValid ? (((uint32_t)d.wday << 16) | ((uint32_t)d.month << 8) | d.mday) : 0xFFFFFFFEu;
    if (dk != lastDateKey) { renderDate(d); lastDateKey = dk; }

    if (!wxDrawn || !wxEqual(d.wx, lastWx)) {
      renderWeather(d.wx);
      lastWx = d.wx; wxDrawn = true;
    }

    renderTicker(d.ticker, now);
  }

 private:
  TFT_eSPI& tft;
  TFT_eSprite tkSpr, timeSpr;
  bool ready = false, needFrame = true;

  // caches de rendu
  uint8_t lastSec = 0xFF, lastColon = 2;
  uint32_t lastDateKey = 0xFFFFFFFFu;
  WeatherData lastWx; bool wxDrawn = false;

  // ticker
  char tkBuf[512] = {0};
  int16_t tkW = 0, tkOff = 0;
  bool tkScroll = false, tkDirty = true;
  uint32_t tkNext = 0, tkHoldUntil = 0;

  static bool wxEqual(const WeatherData& a, const WeatherData& b) {
    return a.valid == b.valid && a.tempC == b.tempC && a.feelsC == b.feelsC &&
           a.windKmh == b.windKmh && a.humidity == b.humidity && a.icon == b.icon &&
           strncmp(a.label, b.label, sizeof(a.label)) == 0;
  }

  // ---------- Fond statique --------------------------------------------------
  void drawFrame() {
    tft.fillScreen(C_BG);
    tft.fillRoundRect(TK_X, TK_Y, TK_W, TK_H, 8, C_CARD);            // ticker
    tft.fillRoundRect(TK_X + 9, TK_Y + 8, 3, 12, 1, C_AMBER);        // barre d'accent
    tft.fillRoundRect(TIME_X, CARD_Y, CARD_W, CARD_H, R_CARD, C_CARD);
    tft.fillRoundRect(WX_X, CARD_Y, CARD_W, CARD_H, R_CARD, C_CARD);
  }

  // ---------- Heure (sprite, zéro flicker) -----------------------------------
  void renderTime(const DashboardData& d, bool colon) {
    char hh[3] = { '-', '-', 0 }, mm[3] = { '-', '-', 0 };
    if (d.timeValid) {
      hh[0] = '0' + d.hour / 10;   hh[1] = '0' + d.hour % 10;
      mm[0] = '0' + d.minute / 10; mm[1] = '0' + d.minute % 10;
    }
    const int16_t cx = TS_W / 2, cy = 24;

    timeSpr.fillSprite(C_CARD);
    timeSpr.setFreeFont(&FreeSansBold24pt7b);
    timeSpr.setTextColor(C_TEXT, C_CARD);
    timeSpr.setTextDatum(MR_DATUM);  timeSpr.drawString(hh, cx - 9, cy);  // positions fixes :
    timeSpr.setTextDatum(ML_DATUM);  timeSpr.drawString(mm, cx + 9, cy);  // pas de gigue
    if (colon) {                                                          // deux-points = 2 pastilles
      timeSpr.fillSmoothCircle(cx, cy - 8, 3, C_AMBER, C_CARD);
      timeSpr.fillSmoothCircle(cx, cy + 8, 3, C_AMBER, C_CARD);
    }
    // Barre des secondes (discrète)
    const int16_t bx = 10, by = 54, bw = TS_W - 20;
    timeSpr.fillRoundRect(bx, by, bw, 4, 2, C_TRACK);
    int16_t fw = d.timeValid ? (int16_t)((uint32_t)d.second * bw / 59) : 0;
    if (fw >= 4) timeSpr.fillRoundRect(bx, by, fw, 4, 2, C_AMBER);
    else if (fw > 0) timeSpr.fillRect(bx, by, fw, 4, C_AMBER);

    timeSpr.pushSprite(TS_X, TS_Y);
  }

  // ---------- Date (rare : dessin direct) ------------------------------------
  void renderDate(const DashboardData& d) {
    static const char* const DAYS[7] = {"Dim.", "Lun.", "Mar.", "Mer.", "Jeu.", "Ven.", "Sam."};
    static const char* const MON[12] = {"Janv.", "Fev.", "Mars", "Avr.", "Mai", "Juin",
                                        "Juil.", "Aout", "Sept.", "Oct.", "Nov.", "Dec."};
    char buf[24] = "";
    if (d.timeValid) snprintf(buf, sizeof(buf), "%s %u %s", DAYS[d.wday % 7], d.mday, MON[(d.month + 11) % 12]);
    tft.fillRect(TIME_X + 4, DATE_CY - 12, CARD_W - 8, 24, C_CARD);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(buf, TIME_X + CARD_W / 2, DATE_CY);
    tft.setTextFont(1);
  }

  // ---------- Météo ----------------------------------------------------------
  // Petit rond "°" dessiné (la police GFX n'a pas de glyphe 0xB0)
  void degree(int16_t x, int16_t y, int16_t r, uint16_t fg) { degreeRing(tft, x, y, r, fg, C_CARD); }

  // Glyphes minimalistes pour la ligne vent / humidité (primitives, ~12 px)
  void windGlyph(int16_t x, int16_t cy) {
    tft.fillRoundRect(x + 2, cy - 5, 10, 2, 1, C_SOFT);
    tft.fillRoundRect(x,     cy - 1, 12, 2, 1, C_SOFT);
    tft.fillRoundRect(x + 1, cy + 3,  8, 2, 1, C_SOFT);
  }
  void dropGlyph(int16_t x, int16_t cy) {
    tft.fillSmoothCircle(x + 4, cy + 2, 4, C_BLUE, C_CARD);
    tft.fillTriangle(x + 4, cy - 7, x, cy + 1, x + 8, cy + 1, C_BLUE);
  }

  // Carte météo : icône (haut) / température (milieu) / 2 lignes compactes (bas).
  // Tout est calculé pour rester dans [CARD_Y+8 ; CARD_Y+CARD_H-8] : rien ne peut déborder.
  void renderWeather(const WeatherData& w) {
    const int16_t cx  = WX_X + CARD_W / 2;
    const int16_t top = CARD_Y + 8, bot = CARD_Y + CARD_H - 8;
    char b[24], c[24];

    // On n'efface que l'intérieur de la carte (les coins arrondis restent intacts)
    tft.fillRect(WX_X + 8, top, CARD_W - 16, bot - top, C_CARD);

    // 1) Icône vectorielle centrée (rayon ~18 px)
    drawWxIcon(tft, cx, CARD_Y + 34, w.valid ? w.icon : WX_CLOUD, 0.72f, C_CARD);

    // 2) Grosse température centrée
    tft.setFreeFont(&FreeSansBold24pt7b);
    tft.setTextColor(C_TEXT, C_CARD);
    tft.setTextDatum(ML_DATUM);
    if (w.valid) snprintf(b, sizeof(b), "%d", (int)w.tempC); else strcpy(b, "--");
    const int16_t tw = tft.textWidth(b);
    const int16_t x0 = cx - (tw + (w.valid ? 12 : 0)) / 2, ty = CARD_Y + 88;
    tft.drawString(b, x0, ty);
    if (w.valid) degree(x0 + tw + 8, ty - 13, 5, C_TEXT);

    // 3) Séparateur + 2 lignes compactes (police 2 : nette, ~7 px/car.)
    tft.setTextFont(1);                                   // gfxFont = NULL avant d'utiliser les polices numérotées
    tft.drawFastHLine(WX_X + 14, CARD_Y + 124, CARD_W - 28, C_TRACK);
    tft.setTextDatum(ML_DATUM);

    // Ligne 1 : "Ressenti 5°"
    const int16_t y1 = CARD_Y + 144, y2 = CARD_Y + 168;
    if (w.valid) snprintf(c, sizeof(c), "%d", (int)w.feelsC); else strcpy(c, "--");
    const int16_t lw = tft.textWidth("Ressenti", 2), vw = tft.textWidth(c, 2);
    const int16_t t1 = lw + 5 + vw + (w.valid ? 7 : 0);
    int16_t x = cx - t1 / 2;
    tft.setTextColor(C_MUTED, C_CARD);  tft.drawString("Ressenti", x, y1, 2);
    x += lw + 5;
    tft.setTextColor(C_TEXT, C_CARD);   tft.drawString(c, x, y1, 2);
    if (w.valid) degreeRing(tft, x + vw + 4, y1 - 4, 3, C_TEXT, C_CARD, 1);

    // Ligne 2 : [vent] 10km/h | [goutte] 94%
    if (w.valid) { snprintf(b, sizeof(b), "%ukm/h", w.windKmh); snprintf(c, sizeof(c), "%u%%", w.humidity); }
    else         { strcpy(b, "--"); strcpy(c, "--"); }
    const int16_t w1 = tft.textWidth(b, 2), w2 = tft.textWidth(c, 2);
    int16_t gap = 8;
    int16_t t2 = 12 + 4 + w1 + gap + 1 + gap + 8 + 4 + w2;
    if (t2 > CARD_W - 16) { gap = 3; t2 = 12 + 4 + w1 + gap + 1 + gap + 8 + 4 + w2; }
    x = cx - t2 / 2;
    windGlyph(x, y2);                  x += 16;
    tft.setTextColor(C_SOFT, C_CARD);  tft.drawString(b, x, y2, 2);  x += w1 + gap;
    tft.drawFastVLine(x, y2 - 7, 14, C_TRACK);                       x += 1 + gap;
    dropGlyph(x, y2);                  x += 12;
    tft.setTextColor(C_SOFT, C_CARD);  tft.drawString(c, x, y2, 2);

    tft.setTextFont(1);
  }

  // ---------- Ticker (sprite, défilement fluide sans String) -----------------
  void renderTicker(const char* text, uint32_t now) {
    if (!text) text = "";
    if (strncmp(text, tkBuf, sizeof(tkBuf) - 1) != 0) {          // texte changé -> reset
      strlcpy(tkBuf, text, sizeof(tkBuf));
      tkSpr.setFreeFont(&FreeSans9pt7b);
      tkW = tkSpr.textWidth(tkBuf);
      tkScroll = tkW > TK_SPR_W;
      tkOff = 0; tkDirty = true; tkHoldUntil = now + TK_PAUSE_MS;
    }
    if (tkScroll) {
      if ((int32_t)(now - tkNext) < 0 || (int32_t)(now - tkHoldUntil) < 0) { if (!tkDirty) return; }
      else {
        tkNext = now + TK_STEP_MS;
        if (++tkOff >= tkW + TK_GAP) { tkOff = 0; tkHoldUntil = now + TK_PAUSE_MS; }
        tkDirty = true;
      }
    }
    if (!tkDirty) return;
    tkDirty = false;

    tkSpr.fillSprite(C_CARD);
    tkSpr.setFreeFont(&FreeSans9pt7b);
    tkSpr.setTextColor(C_SOFT, C_CARD);
    tkSpr.setTextDatum(ML_DATUM);
    tkSpr.drawString(tkBuf, -tkOff, TK_SPR_H / 2);                       // le sprite clippe proprement
    if (tkScroll) tkSpr.drawString(tkBuf, -tkOff + tkW + TK_GAP, TK_SPR_H / 2);
    tkSpr.pushSprite(TK_SPR_X, TK_SPR_Y);
  }
};

}  // namespace ui

// =============================================================================
//  Façade simple : initModernDashboard() puis drawModernDashboard() dans loop()
// =============================================================================
extern TFT_eSPI tft;                       // ton instance globale existante
static ui::Dashboard g_dashboard(tft);

inline bool initModernDashboard()                        { return g_dashboard.begin(); }
inline void endModernDashboard()                         { g_dashboard.end(); }
inline bool modernDashboardReady()                       { return g_dashboard.isReady(); }
inline void invalidateModernDashboard()                  { g_dashboard.invalidate(); }
inline void drawModernDashboard(const ui::DashboardData& d) { g_dashboard.update(d, millis()); }

/* ------------------------------ EXEMPLE ---------------------------------------
  #include "ModernDashboard.h"
  TFT_eSPI tft;                              // doit être défini AVANT l'include si besoin

  void setup() {
    tft.init(); tft.setRotation(1);
    if (!initModernDashboard()) Serial.println("RAM sprites insuffisante");
  }

  void loop() {                              // écran idle uniquement
    static ui::DashboardData d;
    struct tm t;
    if (getLocalTime(&t, 0)) {
      d.hour = t.tm_hour; d.minute = t.tm_min; d.second = t.tm_sec;
      d.wday = t.tm_wday; d.mday = t.tm_mday; d.month = t.tm_mon + 1;
    }
    d.wx = { true, 8, 5, 14, 72, ui::WX_PARTLY, "Peu nuageux" };   // depuis /api/idle
    d.ticker = rssBuffer;                                           // char[] existant
    drawModernDashboard(d);
    // En quittant l'écran idle (lecture) : invalidateModernDashboard();
  }
------------------------------------------------------------------------------- */
