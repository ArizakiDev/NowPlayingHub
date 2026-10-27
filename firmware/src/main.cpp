// ============================================================================
//  Now Playing - ESP32-2432S028R (CYD) - firmware v1.3
//
//  API HTTP (port 80) :
//    POST /api/nowplaying  JSON {state,title,artist,album,position,duration,coverId,app,appId}
//                          -> {ok,needCover,needIcon,cmd?}   (cmd = prev|toggle|next)
//    POST /api/cover?id=X  JPEG 120x120 en base64 (text/plain)
//    POST /api/icon?id=X   JPEG 20x20 en base64 (logo de l'appli)
//    GET/POST /api/config  reglages (brightness, accent "#RRGGBB", scrollSpeed, showApp)
//    GET  /api/status      version, ip, heap...
//    POST /api/update?token=T&size=N   multipart (firmware.bin) -> mise a jour OTA
//    POST /api/idle        JSON {temp,feelsLike,humidity,uvIndex,weatherCode,description,windSpeed,
//                               location,sunrise,sunset,news[]}
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Update.h>
#include <Preferences.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <XPT2046_Touchscreen.h>
#include "mbedtls/base64.h"
#include "esp_wifi.h"
#include "config.h"
#include "settings.h"
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#include <HTTPClient.h>
// ----------------------------- Forward declarations -------------------------
void drawIdleWeatherFull();
void drawIdleNewsFull();
void buildNewsTicker();
void drawMenuScreen();
void drawDayDetail(int day);


// ----------------------------- Couleurs / layout ---------------------------
#define RGB565(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
static uint16_t cAccent = RGB565(250, 45, 72);
#define C_ACCENT cAccent
static const uint16_t C_BG    = RGB565(14, 14, 20);
static const uint16_t C_TEXT  = 0xFFFF;
static const uint16_t C_DIM   = RGB565(140, 140, 155);
static const uint16_t C_TRACK = RGB565(48, 48, 60);
static const uint16_t C_PLACE = RGB565(34, 34, 44);
static const uint16_t C_BTN   = RGB565(38, 38, 50);

static const int SCR_W = 320, SCR_H = 240;
static const int COVER_X = 10, COVER_Y = 34, COVER_SZ = 120;
static const int COL_X = 142, COL_W = 168;           // colonne de droite
static const int TITLE_Y = 40, TITLE_H = 30;
static const int ARTIST_Y = 80, ALBUM_Y = 102;
static const int BTN_Y = 130, BTN_H = 36, BTN_W = 52, BTN_STEP = 58;  // 3 boutons
static const int BAR_X = 10, BAR_Y = 166, BAR_W = 300, BAR_H = 40;

// ----------------------------- Reglages persistants ------------------------
struct Settings {
  uint8_t  brightness = 255;
  uint32_t accent     = 0xFA2D48;
  uint16_t scroll     = 45;
  bool     showApp    = true;
  // --- Mode nuit / veille (reglables depuis le panel web) ---
  bool     nightOn    = true;
  uint8_t  nightStart = 22;   // heure de debut (0-23)
  uint8_t  nightEnd   = 7;    // heure de fin (0-23)
  uint8_t  nightBri   = 8;    // luminosite de nuit en %
  uint8_t  dimMin     = 3;    // minutes sans toucher avant de baisser la luminosite
  uint8_t  dimBri     = 15;   // luminosite apres inactivite en %
} cfg;
Preferences prefs;

void loadSettings() {
  prefs.begin("np", true);
  cfg.brightness = prefs.getUChar("bri", 255);
  cfg.accent     = prefs.getUInt("acc", 0xFA2D48);
  cfg.scroll     = prefs.getUShort("scr", 45);
  cfg.showApp    = prefs.getBool("app", true);
  cfg.nightOn    = prefs.getBool("nOn", true);
  cfg.nightStart = prefs.getUChar("nSt", 22);
  cfg.nightEnd   = prefs.getUChar("nEn", 7);
  cfg.nightBri   = prefs.getUChar("nBr", 8);
  cfg.dimMin     = prefs.getUChar("dMi", 3);
  cfg.dimBri     = prefs.getUChar("dBr", 15);
  prefs.end();
}
void saveSettings() {
  prefs.begin("np", false);
  prefs.putUChar("bri", cfg.brightness);
  prefs.putUInt("acc", cfg.accent);
  prefs.putUShort("scr", cfg.scroll);
  prefs.putBool("app", cfg.showApp);
  prefs.putBool("nOn", cfg.nightOn);
  prefs.putUChar("nSt", cfg.nightStart);
  prefs.putUChar("nEn", cfg.nightEnd);
  prefs.putUChar("nBr", cfg.nightBri);
  prefs.putUChar("dMi", cfg.dimMin);
  prefs.putUChar("dBr", cfg.dimBri);
  prefs.end();
}
uint32_t dynAccent = 0;   // couleur tiree de la pochette (0 = inactive), envoyee par l'appli PC
void applyAccent() {
  uint32_t a = dynAccent ? dynAccent : cfg.accent;
  cAccent = RGB565((a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF);
}

// Retroeclairage en PWM (API LEDC differente entre Arduino-ESP32 2.x et 3.x)
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  static void blInit()          { ledcAttach(TFT_BL, 5000, 8); }
  static void blWrite(uint8_t v){ ledcWrite(TFT_BL, v); }
#else
  static void blInit()          { ledcSetup(7, 5000, 8); ledcAttachPin(TFT_BL, 7); }
  static void blWrite(uint8_t v){ ledcWrite(7, v); }
#endif
void applyBrightness() {
  blWrite(constrain((int)cfg.brightness, 13, 255));
}

// ----------------------------- Objets globaux -----------------------------
TFT_eSPI    tft;
TFT_eSprite titleSpr(&tft);
TFT_eSprite barSpr(&tft);
TFT_eSprite eqSpr(&tft);
WebServer   server(80);
SPIClass    touchSPI(VSPI);
XPT2046_Touchscreen ts(33, 36);   // CS, IRQ

// ----------------------------- Etat lecture --------------------------------
struct Track {
  String title, artist, album, state = "stopped", coverId, app, appId;
  float  duration = 0;
  String lyric;
} np;
bool lyricDirty = false;
float    basePos = 0;
uint32_t baseMs = 0, lastRx = 0;
bool     hasTrack = false;

uint8_t *coverBuf = nullptr, *iconBuf = nullptr;
size_t   coverLen = 0, iconLen = 0;
String   coverIdStored, iconIdStored;

String   pendingCmd;           // commande tactile a transmettre au PC
uint32_t cmdMs = 0;
int      flashIdx = -1;        // bouton surligne
uint32_t flashUntil = 0;

enum Screen { SCR_NONE, SCR_WIFI, SCR_WAIT, SCR_PLAY, SCR_IDLE_WX, SCR_IDLE_NEWS, SCR_MENU };
Screen cur = SCR_NONE;

bool trackDirty = false, coverDirty = false, stateDirty = false, headerDirty = false;
static String wifiError = "";

// ----------------------------- Wi-Fi configurable (portail captif) ----------
#define PORTAL_SSID "NowPlaying-Setup"
String   wifiSsid, wifiPass;     // memorises dans la flash (sinon valeurs de config.h si presentes)
bool     portalActive  = false;  // true -> la carte diffuse son propre reseau de configuration
bool     everConnected = false;
String   portalNets;             // liste <option> des reseaux 2,4 GHz vus au demarrage du portail
DNSServer dnsServer;

void loadWifi() {
  prefs.begin("wifi", true);
  wifiSsid = prefs.getString("ssid", WIFI_SSID);
  wifiPass = prefs.getString("pass", WIFI_PASS);
  prefs.end();
}
static String lastShownWifiErr = "\x01"; // force le premier dessin
int      titleW = 0;
uint32_t scrollStart = 0;
int      lastOff = -9999;
bool     titleStaticDrawn = false;
int      lastSec = -1;

uint32_t lastTouchMs = 0;   // pour l'auto-dim
static int      wxDetailDay   = -1;    // jour selectionne dans meteo plein ecran
static uint32_t lastDetailTap = 0;     // pour double-tap retour depuis detail
int      lastClockMin = -1; // pour le redessin de l'heure dans le header

// ----------------------------- Fullscreen pochette -------------------------
bool fullCover = false;

// ----------------------------- Pause -> Idle --------------------------------
uint32_t pausedSince  = 0;   // millis() quand la piste est passee en pause
bool     forcedIdle   = false; // true si on est en idle force par pause prolongee

// ----------------------------- Donnees IDLE --------------------------------
struct DayForecast { float tMax = 0, tMin = 0; int code = 0; };
struct IdleInfo {
  float  temp        = 0;
  float  feelsLike   = 0;
  float  humidity    = 0;
  float  uvIndex     = 0;
  int    weatherCode = -1;
  String description;
  float  windSpeed   = 0;
  String location;
  String sunrise;
  String sunset;
  String news[10];
  int    newsCount   = 0;
  bool   hasData     = false;
  DayForecast forecast[7];
  int    forecastDays = 0;
} idleData;

// Config locale pour le fetch meteo/RSS sur ESP32 (stockee en Preferences)
struct IdleNetConfig {
  float  lat    = 50.595f;
  float  lon    = 5.117f;
  String loc    = "Fallais";
  String rssUrl = "https://news.google.com/rss?hl=fr&gl=FR&ceid=FR:fr";
  String tz     = "CET-1CEST,M3.5.0,M10.5.0/3";   // fuseau POSIX
} idleCfg;

void loadIdleConfig() {
  prefs.begin("idle", true);
  idleCfg.lat    = prefs.getFloat("ilat",  50.595f);
  idleCfg.lon    = prefs.getFloat("ilon",  5.117f);
  idleCfg.loc    = prefs.getString("iloc", "Fallais");
  idleCfg.rssUrl = prefs.getString("irss", "https://news.google.com/rss?hl=fr&gl=FR&ceid=FR:fr");
  idleCfg.tz     = prefs.getString("itz",  "CET-1CEST,M3.5.0,M10.5.0/3");
  prefs.end();
}
void saveIdleConfig() {
  prefs.begin("idle", false);
  prefs.putFloat("ilat",  idleCfg.lat);
  prefs.putFloat("ilon",  idleCfg.lon);
  prefs.putString("iloc", idleCfg.loc);
  prefs.putString("irss", idleCfg.rssUrl);
  prefs.putString("itz",  idleCfg.tz);
  prefs.end();
}

// Timestamps pour les fetches periodiques ESP32
static uint32_t lastWxFetch  = 0;
static uint32_t lastRssFetch = 0;
static bool     firstFetchDone = false;

// ----------------------------- Layout Idle (utilise des le fetch) -----------
static const int IDLE_NEWS_BAND = 0;   // news tout en haut
static const int IDLE_NEWS_H   = 20;
static const int IDLE_TOPBAR_Y = 22;   // WiFi bars + hint
static const int IDLE_CLK_Y    = 30;   // horloge
static const int IDLE_CLK_FONT = 7;
static const int IDLE_DATE_Y   = 104;  // date
static const int IDLE_SEP_Y    = 122;  // separateur accent
static const int IDLE_WX_Y     = 128;  // meteo
static const int IDLE_WX_H     = 108;  // grande zone meteo
static const int IDLE_NEWS_Y   = IDLE_NEWS_BAND; // alias compat
static const int IDLE_IP_Y     = 236;
static bool     idleDrawn     = false;
static int      idleClockMin  = -1;
static int      idleClockSec  = -1;
static uint32_t lastWifiDraw  = 0;
static uint32_t lastSecsDraw  = 0;
// Description WMO depuis le code meteo
static String wmoDesc(int code) {
  if (code == 0)        return "Ciel degage";
  if (code <= 1)        return "Peu nuageux";
  if (code <= 2)        return "Part. nuageux";
  if (code <= 3)        return "Couvert";
  if (code <= 48)       return "Brouillard";
  if (code <= 55)       return "Bruine";
  if (code <= 65)       return "Pluie";
  if (code <= 67)       return "Pluie verglacante";
  if (code <= 77)       return "Neige";
  if (code <= 82)       return "Averses";
  if (code <= 86)       return "Averse de neige";
  return "Orage";
}

void fetchWeatherFromNet() {
  if (WiFi.status() != WL_CONNECTED) return;
  char url[320];
  snprintf(url, sizeof(url),
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,apparent_temperature,weather_code"
    ",wind_speed_10m,relative_humidity_2m,uv_index"
    "&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min,weather_code"
    "&timezone=auto&forecast_days=7",
    idleCfg.lat, idleCfg.lon);
  HTTPClient http;
  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    JsonDocument doc;
    if (!deserializeJson(doc, body)) {
      JsonObject wxCur = doc["current"].as<JsonObject>();
      idleData.temp        = wxCur["temperature_2m"]      | 0.0f;
      idleData.feelsLike   = wxCur["apparent_temperature"]| 0.0f;
      idleData.weatherCode = wxCur["weather_code"]        | 0;
      idleData.windSpeed   = wxCur["wind_speed_10m"]      | 0.0f;
      idleData.humidity    = wxCur["relative_humidity_2m"]| 0.0f;
      idleData.uvIndex     = wxCur["uv_index"]            | -1.0f;
      idleData.description = wmoDesc(idleData.weatherCode);
      idleData.location    = idleCfg.loc;
      String sr = doc["daily"]["sunrise"][0] | "";
      String ss = doc["daily"]["sunset"][0]  | "";
      idleData.sunrise = (sr.length() >= 16) ? sr.substring(11, 16) : sr;
      idleData.sunset  = (ss.length() >= 16) ? ss.substring(11, 16) : ss;
      // Previsions 7 jours
      idleData.forecastDays = 0;
      JsonArray tMaxArr  = doc["daily"]["temperature_2m_max"];
      JsonArray tMinArr  = doc["daily"]["temperature_2m_min"];
      JsonArray wdCodes  = doc["daily"]["weather_code"];
      for (int i = 0; i < 7 && i < (int)tMaxArr.size(); i++) {
        idleData.forecast[i].tMax = tMaxArr[i] | 0.0f;
        idleData.forecast[i].tMin = tMinArr[i] | 0.0f;
        idleData.forecast[i].code = wdCodes[i]  | 0;
        idleData.forecastDays++;
      }
      idleData.hasData = true;
      if (::cur == SCR_WAIT) idleDrawn = false;
      Serial.printf("[wx-esp] %.1fC code=%d\n", idleData.temp, idleData.weatherCode);
    }
  } else { Serial.printf("[wx-esp] HTTP %d\n", code); }
  http.end();
}

// RSS fetch desactive cote ESP32 (fait par la PC App via /api/idle)
// Les news arrivent via handleIdle() depuis NowPlayingHub.exe
// RSS: fourni par PC App via /api/idle
void fetchRssFromNet() { /* no-op */ }

void tickNetFetch() {
  if (WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  if (!firstFetchDone) {
    firstFetchDone = true;
    fetchWeatherFromNet();
    lastWxFetch = now;
    return;
  }
  if (now - lastWxFetch >= 900000UL) { lastWxFetch = now; fetchWeatherFromNet(); }
}

// Ticker de news : offset de defilement
static int   newsTickerOffset = 0;

static String newsFullStr = "";       // chaine concatenee pour le defilement

// ----------------------------- Utilitaires ---------------------------------
float curPos() {
  float p = basePos;
  if (np.state == "playing") p += (millis() - baseMs) / 1000.0f;
  if (np.duration > 0 && p > np.duration) p = np.duration;
  if (p < 0) p = 0;
  return p;
}

void fmtTime(int s, char* out, size_t n) {
  if (s < 0) s = 0;
  int h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
  if (h > 0) snprintf(out, n, "%d:%02d:%02d", h, m, sec);
  else       snprintf(out, n, "%d:%02d", m, sec);
}

// NTP : on configure apres la connexion WiFi ; flag pour ne le faire qu'une fois
static bool ntpConfigured = false;
static bool ntpSynced     = false;

static void ensureNtp() {
  if (!ntpSynced) {
    struct tm t;
    if (getLocalTime(&t, 5)) {   // 5 ms max
      ntpSynced = (t.tm_year > 100); // annee > 2000
    }
  }
}

// Retourne l'heure locale sous forme "HH:MM" (ou "--:--" si NTP pas encore sync)
static String clockStr() {
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year <= 100) return "--:--";
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return buf;
}
// Retourne les secondes formatees ":SS"
static String secsStr() {
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year <= 100) return ":--";
  char buf[4];
  snprintf(buf, sizeof(buf), ":%02d", t.tm_sec);
  return buf;
}

String fitText(String s, int font, int maxW) {
  if (tft.textWidth(s, font) <= maxW) return s;
  while (s.length() > 1 && tft.textWidth(s + "...", font) > maxW) s.remove(s.length() - 1);
  return s + "...";
}

bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return false;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

// Decode le corps "plain" (base64) d'une requete dans buf ; verifie la signature JPEG
bool decodeJpegBody(uint8_t* buf, size_t cap, size_t& outLen) {
  String b64 = server.arg("plain");
  size_t o = 0;
  int rc = mbedtls_base64_decode(buf, cap, &o, (const uint8_t*)b64.c_str(), b64.length());
  if (rc != 0 || o < 4 || buf[0] != 0xFF || buf[1] != 0xD8) { outLen = 0; return false; }
  outLen = o;
  return true;
}


// ----------------------------- Dessin icone meteo (grande echelle) ---------
void drawWeatherIconLarge(int cx, int cy, int code) {
  // Icones dessinÃ©es Ã  2x par rapport Ã  drawWeatherIcon, zone ~44x44 px
  if (code == 0) {
    tft.fillCircle(cx, cy, 13, RGB565(255, 210, 0));
    for (int a = 0; a < 360; a += 45) {
      float rad = a * 3.14159f / 180.0f;
      int x1 = cx + (int)(17 * cosf(rad)), y1 = cy + (int)(17 * sinf(rad));
      int x2 = cx + (int)(22 * cosf(rad)), y2 = cy + (int)(22 * sinf(rad));
      tft.drawLine(x1, y1, x2, y2, RGB565(255, 210, 0));
      tft.drawLine(x1+1, y1, x2+1, y2, RGB565(255, 210, 0));
    }
  } else if (code <= 2) {
    tft.fillCircle(cx - 7, cy - 4, 10, RGB565(255, 210, 0));
    tft.fillRoundRect(cx - 14, cy + 3, 32, 14, 7, RGB565(210, 210, 220));
    tft.fillCircle(cx - 2, cy + 3, 10, RGB565(210, 210, 220));
    tft.fillCircle(cx + 9, cy + 6, 7, RGB565(210, 210, 220));
  } else if (code <= 3) {
    tft.fillRoundRect(cx - 14, cy + 2, 32, 15, 7, C_DIM);
    tft.fillCircle(cx - 3, cy + 2, 10, C_DIM);
    tft.fillCircle(cx + 8, cy + 5, 8, C_DIM);
  } else if (code <= 48) {
    for (int i = 0; i < 5; i++)
      tft.drawLine(cx - 15, cy - 8 + i * 7, cx + 15, cy - 8 + i * 7, C_DIM);
  } else if (code <= 67) {
    tft.fillRoundRect(cx - 14, cy - 6, 32, 14, 6, RGB565(150, 150, 170));
    tft.fillCircle(cx - 3, cy - 6, 9, RGB565(150, 150, 170));
    tft.fillCircle(cx + 8, cy - 3, 7, RGB565(150, 150, 170));
    for (int i = 0; i < 4; i++)
      tft.fillCircle(cx - 10 + i * 7, cy + 14, 3, RGB565(80, 140, 255));
  } else if (code <= 77) {
    tft.drawLine(cx - 14, cy, cx + 14, cy, C_TEXT);
    tft.drawLine(cx, cy - 14, cx, cy + 14, C_TEXT);
    tft.drawLine(cx - 10, cy - 10, cx + 10, cy + 10, C_TEXT);
    tft.drawLine(cx + 10, cy - 10, cx - 10, cy + 10, C_TEXT);
    tft.fillCircle(cx, cy, 3, C_TEXT);
  } else if (code <= 82) {
    tft.fillRoundRect(cx - 14, cy - 6, 32, 14, 6, RGB565(100, 100, 130));
    tft.fillCircle(cx - 3, cy - 6, 9, RGB565(100, 100, 130));
    tft.fillCircle(cx + 8, cy - 3, 7, RGB565(100, 100, 130));
    for (int i = 0; i < 3; i++)
      tft.fillCircle(cx - 8 + i * 8, cy + 14, 4, RGB565(60, 120, 255));
  } else {
    tft.fillCircle(cx, cy - 8, 11, RGB565(90, 90, 120));
    tft.fillTriangle(cx + 3, cy - 3, cx - 4, cy + 6, cx + 6, cy + 6, RGB565(255, 230, 0));
    tft.fillTriangle(cx - 3, cy + 5, cx - 6, cy + 14, cx + 4, cy + 14, RGB565(255, 230, 0));
  }
}

// ============================================================
//  ECRAN IDLE v3 - Layout definitif (voir constantes en haut de fichier)
// ============================================================

static const char* dayNames[]   = {"Dimanche","Lundi","Mardi","Mercredi","Jeudi","Vendredi","Samedi"};
static const char* monthNames[] = {"janvier","fevrier","mars","avril","mai","juin",
                                   "juillet","aout","septembre","octobre","novembre","decembre"};

void buildNewsTicker() {
  newsFullStr = "";
  for (int i = 0; i < idleData.newsCount; i++) {
    if (i > 0) newsFullStr += "  -  ";
    newsFullStr += idleData.news[i];
  }
  newsTickerOffset = 0;
}

void drawWifiBars(int bx, int by) {
  int rssi = WiFi.RSSI();
  bool connected = (WiFi.status() == WL_CONNECTED);
  uint16_t barColor; int activeBars;
  if (!connected)       { barColor = RGB565(220,50,50);  activeBars = 1; }
  else if (rssi > -60)  { barColor = RGB565(50,200,80);  activeBars = 4; }
  else if (rssi > -75)  { barColor = RGB565(255,160,0);  activeBars = 3; }
  else                  { barColor = RGB565(220,50,50);  activeBars = 2; }
  int heights[4] = {8,13,18,23}, barW = 5, gap = 3;
  for (int i = 0; i < 4; i++) {
    int h = heights[i], x = bx + i*(barW+gap), y = by + (23-h);
    tft.fillRect(x, by, barW, 23, C_BG);
    tft.fillRect(x, y, barW, h, (i < activeBars) ? barColor : RGB565(55,55,70));
  }
}

void drawIdleClock() {
  tft.fillRect(0, IDLE_CLK_Y, SCR_W, IDLE_SEP_Y - IDLE_CLK_Y + 4, C_BG);
  String clk = clockStr();
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(clk, SCR_W / 2, IDLE_CLK_Y, IDLE_CLK_FONT);
  int clkW = tft.textWidth(clk, IDLE_CLK_FONT);
  int secX = SCR_W / 2 + clkW / 2 + 5;
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(secsStr(), secX, IDLE_CLK_Y + 52, 2);
}

void drawIdleDate() {
  tft.fillRect(0, IDLE_DATE_Y, SCR_W, 22, C_BG);
  struct tm tm;
  if (!getLocalTime(&tm, 0)) return;
  idleClockMin = tm.tm_min;
  char buf[50];
  snprintf(buf, sizeof(buf), "%s %d %s", dayNames[tm.tm_wday], tm.tm_mday, monthNames[tm.tm_mon]);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(buf, SCR_W / 2, IDLE_DATE_Y + 2, 2);
}

void drawIdleWeather() {
  tft.fillRect(0, IDLE_WX_Y, SCR_W, IDLE_WX_H, C_BG);
  if (!idleData.hasData) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(RGB565(60,60,80), C_BG);
    tft.drawString("En attente meteo...", SCR_W/2, IDLE_WX_Y + 34, 2);
    return;
  }
  const int TXT_X  = 70;
  const int TEMP_Y = IDLE_WX_Y + 2;

  if (idleData.sunrise.length() && idleData.sunset.length()) {
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(RGB565(210,160,50), C_BG);
    tft.drawString("^ " + idleData.sunrise + "  v " + idleData.sunset, SCR_W-4, IDLE_WX_Y+2, 1);
    if (idleData.uvIndex >= 0) {
      char uvb[10]; snprintf(uvb, sizeof(uvb), "UV %.0f", idleData.uvIndex);
      tft.setTextColor(RGB565(180,120,220), C_BG);
      tft.drawString(uvb, SCR_W-4, IDLE_WX_Y+13, 1);
    }
  }

  char tempBuf[12];
  snprintf(tempBuf, sizeof(tempBuf), "%.0fC", idleData.temp);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(tempBuf, TXT_X, TEMP_Y, 6);

  char descBuf[50];
  if (idleData.windSpeed > 0.5f)
    snprintf(descBuf, sizeof(descBuf), "%s  %.0f km/h", idleData.description.c_str(), idleData.windSpeed);
  else
    snprintf(descBuf, sizeof(descBuf), "%s", idleData.description.c_str());
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(fitText(String(descBuf), 2, SCR_W - TXT_X - 4), TXT_X, TEMP_Y + 50, 2);

  char feelBuf[36];
  snprintf(feelBuf, sizeof(feelBuf), "Res. %.0fC   Hum %.0f%%", idleData.feelsLike, idleData.humidity);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(feelBuf, TXT_X, TEMP_Y + 72, 2);

  drawWeatherIconLarge(32, IDLE_WX_Y + 38, idleData.weatherCode);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(RGB565(58,58,78), C_BG);
  tft.drawString(idleData.location, SCR_W-4, IDLE_WX_Y + IDLE_WX_H - 10, 1);
}

void drawIdleScreen() {
  tft.fillScreen(C_BG);
  // Bandeau news tout en haut
  tft.fillRect(0, IDLE_NEWS_BAND, SCR_W, IDLE_NEWS_H, RGB565(12,12,22));
  tft.fillRect(0, IDLE_NEWS_H, SCR_W, 2, C_TRACK);
  // Hint double-tap discret
  if (forcedIdle && hasTrack) {
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(RGB565(45,45,65), C_BG);
    tft.drawString("2x tap", SCR_W-4, IDLE_TOPBAR_Y + 2, 1);
  }
  // Horloge + date
  drawIdleClock();
  drawIdleDate();
  // Separateur avant meteo
  tft.fillRect(24, IDLE_SEP_Y, SCR_W-48, 2, C_ACCENT);
  // Meteo
  drawIdleWeather();
  // IP discret
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(RGB565(38,38,52), C_BG);
  tft.drawString(WiFi.localIP().toString(), SCR_W-4, IDLE_IP_Y, 1);
  if (idleData.newsCount > 0) buildNewsTicker();
  idleDrawn    = true;
  lastWifiDraw = 0;
}

void updateIdleClock() {
  drawIdleClock();
  drawIdleDate();
  tft.fillRect(24, IDLE_SEP_Y, SCR_W-48, 2, C_ACCENT);
}

void tickNewsTicker() {
  if (idleData.newsCount == 0 || newsFullStr.length() == 0) return;
  static uint32_t lastTick = 0;
  if (millis() - lastTick < 40) return;
  lastTick = millis();
  static TFT_eSprite* tickerSpr = nullptr;
  if (!tickerSpr) {
    tickerSpr = new TFT_eSprite(&tft);
    tickerSpr->createSprite(SCR_W, 18);
  }
  int fullW = tft.textWidth(newsFullStr, 2);
  tickerSpr->fillSprite(C_BG);
  tickerSpr->setTextDatum(TL_DATUM);
  tickerSpr->setTextColor(RGB565(160,165,195), C_BG);
  tickerSpr->drawString(newsFullStr, -newsTickerOffset, 1, 2);
  if (fullW > 0)
    tickerSpr->drawString(newsFullStr, -newsTickerOffset + fullW + 40, 1, 2);
  // pushSprite dans la zone news (bandeau au-dessus de la meteo)
  tickerSpr->pushSprite(0, IDLE_NEWS_BAND + 1);
  newsTickerOffset++;
  if (fullW > 0 && newsTickerOffset >= fullW + 40) newsTickerOffset = 0;
}

void animateIdle() {
  if (!idleDrawn) { drawIdleScreen(); return; }
  if (millis() - lastSecsDraw > 1000) {
    lastSecsDraw = millis();
    struct tm tm;
    if (getLocalTime(&tm, 0) && tm.tm_year > 100) {
      if (tm.tm_sec != idleClockSec) {
        idleClockSec = tm.tm_sec;
        if (tm.tm_min != idleClockMin) {
          updateIdleClock();
        } else {
          String clk = clockStr();
          int clkW = tft.textWidth(clk, IDLE_CLK_FONT);
          int secX = SCR_W / 2 + clkW / 2 + 5;
          tft.fillRect(secX, IDLE_CLK_Y + 50, 28, 14, C_BG);
          tft.setTextDatum(TL_DATUM);
          tft.setTextColor(C_DIM, C_BG);
          tft.drawString(secsStr(), secX, IDLE_CLK_Y + 52, 2);
        }
      }
    }
  }
  tickNewsTicker();
}


// ----------------------------- Dessin : ecran lecture ----------------------
void drawStateIcon() {
  tft.fillRect(288, 2, 26, 22, C_BG);
  if (np.state == "playing") {
    tft.fillTriangle(294, 5, 294, 19, 307, 12, C_ACCENT);
  } else {
    tft.fillRoundRect(293, 5, 5, 14, 1, C_DIM);
    tft.fillRoundRect(302, 5, 5, 14, 1, C_DIM);
  }
}

void drawHeader() {
  tft.fillRect(0, 0, SCR_W, 28, C_BG);
  int x = 10;
  bool hasIcon = cfg.showApp && iconLen > 0 && np.appId.length() && iconIdStored == np.appId;
  if (hasIcon) { TJpgDec.drawJpg(10, 4, iconBuf, iconLen); x = 36; }
  String label = (cfg.showApp && np.app.length()) ? np.app : String("NOW PLAYING");
  label.toUpperCase();
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString(fitText(label, 2, 215 - x), x, 6, 2);  // limite a 215 pour laisser place a l'heure
  // Heure NTP (a droite, avant l'icone etat)
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(clockStr(), 280, 6, 2);
  drawStateIcon();
}

void drawButtons(int hi = -1) {
  for (int i = 0; i < 3; i++) {
    int x = COL_X + i * BTN_STEP, cx = x + BTN_W / 2, cy = BTN_Y + BTN_H / 2;
    tft.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 8, (i == hi) ? C_ACCENT : C_BTN);
    if (i == 0) {                       // precedent
      tft.fillRect(cx - 9, cy - 8, 3, 16, C_TEXT);
      tft.fillTriangle(cx + 9, cy - 8, cx + 9, cy + 8, cx - 5, cy, C_TEXT);
    } else if (i == 1) {                // pause si lecture, sinon play
      if (np.state == "playing") {
        tft.fillRect(cx - 7, cy - 8, 5, 16, C_TEXT);
        tft.fillRect(cx + 2, cy - 8, 5, 16, C_TEXT);
      } else {
        tft.fillTriangle(cx - 6, cy - 9, cx - 6, cy + 9, cx + 8, cy, C_TEXT);
      }
    } else {                            // suivant
      tft.fillTriangle(cx - 9, cy - 8, cx - 9, cy + 8, cx + 5, cy, C_TEXT);
      tft.fillRect(cx + 6, cy - 8, 3, 16, C_TEXT);
    }
  }
}

void drawCover() {
  bool ok = coverLen > 0 && np.coverId.length() && coverIdStored == np.coverId;
  if (ok) { TJpgDec.drawJpg(COVER_X, COVER_Y, coverBuf, coverLen); return; }
  tft.fillRoundRect(COVER_X, COVER_Y, COVER_SZ, COVER_SZ, 10, C_PLACE);
  int cx = COVER_X + COVER_SZ / 2, cy = COVER_Y + COVER_SZ / 2;
  tft.fillCircle(cx - 10, cy + 22, 12, C_DIM);
  tft.fillRect(cx + 1, cy - 32, 5, 54, C_DIM);
  tft.fillTriangle(cx + 6, cy - 32, cx + 28, cy - 20, cx + 6, cy - 8, C_DIM);
}

// Dessin pochette plein ecran (200x200 centree)
void drawFullCoverScreen() {
  tft.fillScreen(C_BG);
  bool ok = coverLen > 0 && np.coverId.length() && coverIdStored == np.coverId;
  if (ok) {
    // On decode le JPEG directement a la taille originale (120x120) et on le place centre
    // Pour un vrai scale up il faudrait un decoder supplementaire; on place simplement au centre
    int cx = (SCR_W - COVER_SZ) / 2;
    int cy = (SCR_H - COVER_SZ) / 2;
    TJpgDec.drawJpg(cx, cy, coverBuf, coverLen);
  } else {
    // Placeholder centre
    int cx = (SCR_W - COVER_SZ) / 2;
    int cy = (SCR_H - COVER_SZ) / 2;
    tft.fillRoundRect(cx, cy, COVER_SZ, COVER_SZ, 10, C_PLACE);
    int icx = cx + COVER_SZ / 2, icy = cy + COVER_SZ / 2;
    tft.fillCircle(icx - 10, icy + 22, 12, C_DIM);
    tft.fillRect(icx + 1, icy - 32, 5, 54, C_DIM);
    tft.fillTriangle(icx + 6, icy - 32, icx + 28, icy - 20, icx + 6, icy - 8, C_DIM);
  }
  // Indication de retour (petit texte en bas)
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RGB565(60, 60, 75), C_BG);
  tft.drawString("Tap pour revenir", SCR_W / 2, SCR_H - 4, 2);
}

void drawArtistAlbum() {
  tft.fillRect(COL_X, ARTIST_Y - 2, COL_W, 48, C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(fitText(np.artist, 2, COL_W), COL_X, ARTIST_Y, 2);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(fitText(np.album, 2, COL_W), COL_X, ALBUM_Y, 2);
}

void renderTitleSprite(int off) {
  titleSpr.fillSprite(C_BG);
  titleSpr.setTextDatum(TL_DATUM);
  titleSpr.setTextColor(C_TEXT, C_BG);
  titleSpr.drawString(np.title, -off, 2, 4);
  if (titleW > COL_W) titleSpr.drawString(np.title, -off + titleW + 40, 2, 4);
  titleSpr.pushSprite(COL_X, TITLE_Y);
}

void layoutTitle() {
  titleW = tft.textWidth(np.title, 4);
  titleStaticDrawn = false;
  scrollStart = millis();
  lastOff = -9999;
}

void tickTitle() {
  if (titleW <= COL_W) {
    if (!titleStaticDrawn) { renderTitleSprite(0); titleStaticDrawn = true; }
    return;
  }
  static uint32_t lastMs = 0;
  if (millis() - lastMs < 33) return;
  lastMs = millis();
  const uint32_t PAUSE_MS = 1500;
  int speed = max((int)cfg.scroll, 10);
  int cycle = titleW + 40;
  uint32_t period = PAUSE_MS + (uint32_t)cycle * 1000UL / speed;
  uint32_t t = (millis() - scrollStart) % period;
  int off = (t < PAUSE_MS) ? 0 : (int)((t - PAUSE_MS) * speed / 1000UL);
  if (off == lastOff) return;
  lastOff = off;
  renderTitleSprite(off);
}

void tickBar(bool force = false) {
  float pos = curPos();
  int sec = (int)pos;
  if (!force && sec == lastSec) return;
  lastSec = sec;

  barSpr.fillSprite(C_BG);
  barSpr.fillRoundRect(0, 4, BAR_W, 6, 3, C_TRACK);
  int fw = (np.duration > 0) ? (int)(BAR_W * pos / np.duration) : 0;
  if (fw >= 6)     barSpr.fillRoundRect(0, 4, fw, 6, 3, C_ACCENT);
  else if (fw > 0) barSpr.fillRect(0, 4, fw, 6, C_ACCENT);
  if (fw > 0)      barSpr.fillCircle(constrain(fw, 5, BAR_W - 5), 7, 5, C_TEXT);

  char a[12], b[12];
  fmtTime(sec, a, sizeof(a));
  if (np.duration > 0) fmtTime((int)np.duration, b, sizeof(b)); else strcpy(b, "--:--");
  barSpr.setTextColor(C_DIM, C_BG);
  barSpr.setTextDatum(TL_DATUM); barSpr.drawString(a, 0, 20, 2);
  barSpr.setTextDatum(TR_DATUM); barSpr.drawString(b, BAR_W, 20, 2);
  barSpr.pushSprite(BAR_X, BAR_Y);
}

// Paroles : jusqu'a 2 lignes centrees (coupe aux espaces), zone sous la barre de progression
void drawLyric() {
  const int LY_TOP = BAR_Y + BAR_H;          // 206
  const int LY_H   = SCR_H - LY_TOP;         // 34 px = 2 lignes de police 2
  const int LINE_H = 16;
  const int MAXW   = SCR_W - 8;
  tft.fillRect(0, LY_TOP, SCR_W, LY_H, C_BG);
  if (!np.lyric.length()) return;
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);

  String src = np.lyric, l1 = src, l2 = "";
  if (tft.textWidth(src, 2) > MAXW) {
    int cut = -1;                              // dernier espace qui tient sur la ligne 1
    for (int i = 0; i < (int)src.length(); i++) {
      if (src[i] != ' ') continue;
      if (tft.textWidth(src.substring(0, i), 2) > MAXW) break;
      cut = i;
    }
    if (cut < 0) {                             // mot trop long : coupe brutale
      cut = src.length();
      while (cut > 1 && tft.textWidth(src.substring(0, cut), 2) > MAXW) cut--;
      l1 = src.substring(0, cut);
      l2 = src.substring(cut);
    } else {
      l1 = src.substring(0, cut);
      l2 = src.substring(cut + 1);
    }
    l2 = fitText(l2, 2, MAXW);                 // securite : "..." si ca depasse encore
  }

  if (l2.length()) {
    tft.drawString(l1, SCR_W / 2, LY_TOP + 1, 2);
    tft.drawString(l2, SCR_W / 2, LY_TOP + 1 + LINE_H, 2);
  } else {
    tft.drawString(l1, SCR_W / 2, LY_TOP + (LY_H - LINE_H) / 2, 2);   // 1 ligne : centree verticalement
  }
}

void drawPlayScreen() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawButtons();
  trackDirty = coverDirty = true;
  stateDirty = headerDirty = false;
  lastSec = -1;
  lyricDirty = true;
}

void renderPlay() {
  if (fullCover) {
    // En mode plein ecran pochette, pas de mise a jour normale
    return;
  }
  if (headerDirty) { headerDirty = false; drawHeader(); }
  if (trackDirty)  { trackDirty = false; layoutTitle(); drawArtistAlbum(); }
  if (coverDirty)  { coverDirty = false; drawCover(); }
  if (lyricDirty)  { lyricDirty = false; drawLyric(); }
  if (stateDirty)  { stateDirty = false; drawStateIcon(); drawButtons(flashIdx); tickBar(true); }
  if (flashIdx >= 0 && millis() > flashUntil) { flashIdx = -1; drawButtons(); }
  tickTitle();
  tickBar();
}

// ----------------------------- Dessin : ecran d'attente --------------------
void drawWaitScreen(bool wifiScreen) {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Now Playing", SCR_W / 2, 122, 4);
  tft.setTextColor(C_ACCENT, C_BG);
  if (wifiScreen) {
    if (portalActive) {
      tft.drawString("Configuration du Wi-Fi", SCR_W / 2, 158, 2);
      tft.setTextColor(C_DIM, C_BG);
      tft.drawString("Connecte-toi a " PORTAL_SSID, SCR_W / 2, 182, 2);
      tft.drawString("puis ouvre 192.168.4.1", SCR_W / 2, 204, 2);
    } else {
    tft.drawString("Connexion Wi-Fi...", SCR_W / 2, 158, 2);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString(wifiSsid.length() ? wifiSsid : String("(aucun reseau)"), SCR_W / 2, 182, 2);
    if (wifiError.length()) {
      tft.setTextColor(RGB565(255, 80, 80), C_BG);
      tft.drawString(wifiError, SCR_W / 2, 204, 2);
    }
    }
  } else {
    tft.drawString("En attente de la musique", SCR_W / 2, 158, 2);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Lance l'appli PC + une lecture", SCR_W / 2, 182, 2);
    tft.drawString(WiFi.localIP().toString() + "  |  " HOSTNAME ".local", SCR_W / 2, 204, 2);
  }
  tft.setTextColor(C_TRACK, C_BG);
  tft.drawString("v" FW_VERSION, SCR_W / 2, 224, 2);
}

void animateWait() {
  static uint32_t t = 0;
  if (millis() - t < 80) return;
  t = millis();
  eqSpr.fillSprite(C_BG);
  for (int i = 0; i < 5; i++) {
    int h = 10 + (int)(24 + 24 * sinf(millis() / 260.0f + i * 1.3f));
    eqSpr.fillRoundRect(6 + i * 20, 70 - h, 10, h, 3, C_ACCENT);
  }
  eqSpr.pushSprite(110, 30);
  // Mise a jour de l'horloge sur l'ecran d'attente chaque minute
  struct tm tm;
  if (getLocalTime(&tm, 0)) {
    if (tm.tm_min != lastClockMin) {
      lastClockMin = tm.tm_min;
      char buf[6];
      snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(C_DIM, C_BG);
      tft.drawString(buf, SCR_W / 2, 224, 2);
    }
  }
}

// ----------------------------- Auto-dim + Mode nuit -----------------------
bool autoDimReset = true;   // true -> reapplique la luminosite au prochain tick
void tickAutoDim() {
  static bool dimmed = false;
  static bool nightMode = false;

  // Mode nuit via l'heure NTP (gere aussi les plages qui passent minuit, ex. 22h -> 7h)
  bool isNight = false;
  struct tm tm;
  if (cfg.nightOn && getLocalTime(&tm, 0)) {
    int h = tm.tm_hour;
    if (cfg.nightStart == cfg.nightEnd)      isNight = false;
    else if (cfg.nightStart < cfg.nightEnd)  isNight = (h >= cfg.nightStart && h < cfg.nightEnd);
    else                                     isNight = (h >= cfg.nightStart || h < cfg.nightEnd);
  }

  const uint8_t nightPwm = (uint8_t)max(3, (int)cfg.nightBri * 255 / 100);
  const uint8_t dimPwm   = (uint8_t)max(3, (int)cfg.dimBri   * 255 / 100);
  const uint8_t normPwm  = (uint8_t)constrain((int)cfg.brightness, 13, 255);
  const bool shouldDim   = (millis() - lastTouchMs > (uint32_t)cfg.dimMin * 60000UL);

  if (autoDimReset || isNight != nightMode) {
    autoDimReset = false;
    nightMode = isNight;
    dimmed = shouldDim;
    blWrite(nightMode ? nightPwm : (dimmed ? dimPwm : normPwm));
    return;
  }
  if (nightMode) { blWrite(nightPwm); return; }

  if (shouldDim == dimmed) return;
  dimmed = shouldDim;
  blWrite(dimmed ? dimPwm : normPwm);
}

// ----------------------------- Tactile -------------------------------------
void pressButton(int i) {
  static const char* cmds[3] = { "prev", "toggle", "next" };
  pendingCmd = cmds[i];
  cmdMs = millis();
  flashIdx = i;
  flashUntil = millis() + 180;
  drawButtons(i);
}

void handleTouch() {
  static bool     down   = false;
  static uint32_t last   = 0;
  static int      startX = -1;
  static int      startY = -1;
  static uint32_t lastIdleTap = 0;  // pour double-tap sur idle

  if (!ts.touched()) {
    if (down) {
      if (cur == SCR_PLAY && startX >= 0) {
        TS_Point p = ts.getPoint();
        int endX = map(p.x, 200, 3700, 1, SCR_W);
        int endY = map(p.y, 240, 3800, 1, SCR_H);
        int dx = endX - startX, dy = endY - startY;
        if (fullCover) {
          fullCover = false; drawPlayScreen(); coverDirty = true; trackDirty = true;
        } else if (abs(dx) > 40 && abs(dx) > abs(dy)) {
          pressButton(dx > 0 ? 0 : 2);  // droite=prev, gauche=next
        }
      }
    }
    down = false; startX = startY = -1;
    return;
  }

  lastTouchMs = millis();

  if (down || millis() - last < 250) return;
  down = true;
  last = millis();

  TS_Point p = ts.getPoint();
  int x = map(p.x, 200, 3700, 1, SCR_W);
  int y = map(p.y, 240, 3800, 1, SCR_H);

  // Ecran MENU
  if (cur == SCR_MENU) {
    // Bouton + brightness (zone droite)
    if (x > SCR_W-50 && y > 80 && y < 130) {
      cfg.brightness = constrain((int)cfg.brightness + 20, 13, 255);
      saveSettings(); applyBrightness();
      drawMenuScreen(); return;
    }
    // Bouton - brightness (zone gauche)
    if (x < 50 && y > 80 && y < 130) {
      cfg.brightness = constrain((int)cfg.brightness - 20, 13, 255);
      saveSettings(); applyBrightness();
      drawMenuScreen(); return;
    }
    // Bouton Reboot
    if (y > 160 && y < 200) { ESP.restart(); }
    // Retour idle
    cur = SCR_WAIT; idleDrawn = false; return;
  }

  // Ecrans meteo/news : DOUBLE-TAP pour revenir
  if (cur == SCR_IDLE_WX) {
    // Si on a un detail jour affiche
    if (wxDetailDay >= 0) {
      wxDetailDay = -1; drawIdleWeatherFull(); return;
    }
    // Tap sur zone previsions (bas) -> detail du jour
    if (y > 150 && idleData.forecastDays > 0) {
      int col = x / (SCR_W / idleData.forecastDays);
      col = constrain(col, 0, idleData.forecastDays - 1);
      wxDetailDay = col;
      drawDayDetail(col); return;
    }
    // Double-tap sur zone superieure -> retour idle
    uint32_t now = millis();
    if (now - lastDetailTap < 600) {
      wxDetailDay = -1; cur = SCR_WAIT; idleDrawn = false;
    } else {
      lastDetailTap = now;
      // flash hint
      tft.setTextDatum(BC_DATUM); tft.setTextColor(C_ACCENT, C_BG);
      tft.drawString("2x tap pour revenir", SCR_W/2, 239, 1);
    }
    return;
  }
  if (cur == SCR_IDLE_NEWS) {
    uint32_t now = millis();
    if (now - lastDetailTap < 600) {
      cur = SCR_WAIT; idleDrawn = false;
    } else {
      lastDetailTap = now;
      tft.setTextDatum(BC_DATUM); tft.setTextColor(C_ACCENT, C_BG);
      tft.drawString("2x tap pour revenir", SCR_W/2, 239, 1);
    }
    return;
  }

  // Idle : tap top-right -> menu systeme
  if (cur == SCR_WAIT && x > SCR_W - 30 && y < 30) {
    cur = SCR_MENU; drawMenuScreen(); return;
  }
  // Idle : tap zone -> details, double-tap -> retour musique
  if (cur == SCR_WAIT) {
    uint32_t now = millis();
    if (now - lastIdleTap < 500 && hasTrack) {
      forcedIdle = false; pausedSince = 0; cur = SCR_NONE;
      lastIdleTap = 0; return;
    }
    lastIdleTap = now;
    if (y >= IDLE_NEWS_BAND && y <= IDLE_NEWS_BAND + IDLE_NEWS_H) {
      cur = SCR_IDLE_NEWS; drawIdleNewsFull(); return;
    }
    if (y >= IDLE_WX_Y && y <= IDLE_WX_Y + IDLE_WX_H) {
      cur = SCR_IDLE_WX; drawIdleWeatherFull(); return;
    }
    return;
  }

  if (cur != SCR_PLAY) return;

  startX = x; startY = y;
  if (fullCover) return;

  // Tap pochette â†’ fullscreen
  if (x >= COVER_X && x <= COVER_X + COVER_SZ && y >= COVER_Y && y <= COVER_Y + COVER_SZ) {
    fullCover = true; drawFullCoverScreen(); return;
  }

  // Boutons
  if (y >= BTN_Y - 6 && y <= BTN_Y + BTN_H + 6) {
    for (int i = 0; i < 3; i++) {
      int bx = COL_X + i * BTN_STEP;
      if (x >= bx - 4 && x <= bx + BTN_W + 4) { pressButton(i); return; }
    }
  }
}

// ----------------------------- Handlers HTTP -------------------------------
void sendReply(bool needCover, bool needIcon) {
  JsonDocument r;
  r["ok"] = true;
  r["needCover"] = needCover;
  r["needIcon"] = needIcon;
  if (pendingCmd.length()) {
    if (millis() - cmdMs < 4000) r["cmd"] = pendingCmd;   // commande trop vieille = ignoree
    pendingCmd = "";
  }
  String out;
  serializeJson(r, out);
  server.send(200, "application/json", out);
}

void handleNowPlaying() {
  JsonDocument d;
  if (!server.hasArg("plain") || deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  lastRx = millis();
  String st = d["state"] | "stopped";
  if (st == "stopped") {
    np.state = "stopped";
    hasTrack = false;
    if (dynAccent) { dynAccent = 0; applyAccent(); }
    np.lyric = "";
    sendReply(false, false);
    return;
  }

  String title = d["title"] | "", artist = d["artist"] | "", album = d["album"] | "";
  String cid = d["coverId"] | "", app = d["app"] | "", appId = d["appId"] | "";
  bool changed = !hasTrack || title != np.title || artist != np.artist || album != np.album;
  if (changed) {
    np.title = title; np.artist = artist; np.album = album; np.coverId = cid;
    np.lyric = ""; lyricDirty = true;
    trackDirty = true;
    coverDirty = true;
  }
  if (app != np.app || appId != np.appId) { np.app = app; np.appId = appId; headerDirty = true; }
  if (np.state != st) {
    np.state = st;
    stateDirty = true;
    // Tracker quand la piste devient en pause
    if (st == "paused" && pausedSince == 0) pausedSince = millis();
    else if (st == "playing")               { pausedSince = 0; forcedIdle = false; }
  }
  // Couleur d'accent de la pochette (optionnelle)
  {
    String ac = d["accent"] | "";
    uint32_t na = (ac.length() == 7 && ac[0] == '#') ? ((uint32_t)strtoul(ac.c_str() + 1, nullptr, 16) & 0xFFFFFF) : 0;
    if (na != dynAccent) { dynAccent = na; applyAccent(); stateDirty = true; headerDirty = true; }
  }
  // Ligne de paroles en cours (optionnelle)
  {
    String ly = d["lyric"] | "";
    if (ly != np.lyric) { np.lyric = ly; lyricDirty = true; }
  }
  np.duration = d["duration"] | 0.0f;
  basePos = d["position"] | 0.0f;
  baseMs = millis();
  hasTrack = true;

  sendReply(cid.length() > 0 && cid != coverIdStored,
            cfg.showApp && appId.length() > 0 && appId != iconIdStored);
}

void handleCover() {
  if (!decodeJpegBody(coverBuf, COVER_MAX_BYTES, coverLen)) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  coverIdStored = server.arg("id");
  coverDirty = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleIcon() {
  if (!decodeJpegBody(iconBuf, ICON_MAX_BYTES, iconLen)) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  iconIdStored = server.arg("id");
  headerDirty = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void sendConfig() {
  JsonDocument d;
  char hex[8];
  snprintf(hex, sizeof(hex), "#%06X", (unsigned)(cfg.accent & 0xFFFFFF));
  d["brightness"] = cfg.brightness;
  d["accent"] = hex;
  d["scrollSpeed"] = cfg.scroll;
  d["showApp"] = cfg.showApp;
  d["nightOn"] = cfg.nightOn;
  d["nightStart"] = cfg.nightStart;
  d["nightEnd"] = cfg.nightEnd;
  d["nightBri"] = cfg.nightBri;
  d["dimMin"] = cfg.dimMin;
  d["dimBri"] = cfg.dimBri;
  d["version"] = FW_VERSION;
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void handleConfigPost() {
  JsonDocument d;
  if (!server.hasArg("plain") || deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  if (!d["brightness"].isNull()) cfg.brightness = (uint8_t)constrain(d["brightness"].as<int>(), 5, 100);
  if (!d["scrollSpeed"].isNull()) cfg.scroll = (uint16_t)constrain(d["scrollSpeed"].as<int>(), 10, 150);
  if (!d["showApp"].isNull()) cfg.showApp = d["showApp"].as<bool>();
  if (!d["nightOn"].isNull())    cfg.nightOn    = d["nightOn"].as<bool>();
  if (!d["nightStart"].isNull()) cfg.nightStart = (uint8_t)constrain(d["nightStart"].as<int>(), 0, 23);
  if (!d["nightEnd"].isNull())   cfg.nightEnd   = (uint8_t)constrain(d["nightEnd"].as<int>(), 0, 23);
  if (!d["nightBri"].isNull())   cfg.nightBri   = (uint8_t)constrain(d["nightBri"].as<int>(), 1, 100);
  if (!d["dimMin"].isNull())     cfg.dimMin     = (uint8_t)constrain(d["dimMin"].as<int>(), 1, 60);
  if (!d["dimBri"].isNull())     cfg.dimBri     = (uint8_t)constrain(d["dimBri"].as<int>(), 1, 100);
  if (!d["accent"].isNull()) {
    String a = d["accent"].as<String>();
    if (a.length() == 7 && a[0] == '#') cfg.accent = (uint32_t)strtoul(a.c_str() + 1, nullptr, 16) & 0xFFFFFF;
  }
  saveSettings();
  applyBrightness();
  applyAccent();
  autoDimReset = true;
  cur = SCR_NONE;                     // force un redessin complet avec les nouveaux reglages
  sendConfig();
}

void handleStatus() {
  JsonDocument d;
  d["version"] = FW_VERSION;
  d["ip"] = WiFi.localIP().toString();
  d["state"] = np.state;
  d["heap"] = ESP.getFreeHeap();
  d["rssi"] = WiFi.RSSI();
  d["uptime"] = millis() / 1000;
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void handleIdle() {
  JsonDocument d;
  if (!server.hasArg("plain") || deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }

  idleData.temp        = d["temp"]        | 0.0f;
  idleData.feelsLike   = d["feelsLike"]   | 0.0f;
  idleData.humidity    = d["humidity"]    | 0.0f;
  idleData.uvIndex     = d["uvIndex"]     | -1.0f;
  idleData.weatherCode = d["weatherCode"] | -1;
  idleData.description = (const char*)(d["description"] | "");
  idleData.windSpeed   = d["windSpeed"]   | 0.0f;
  idleData.location    = (const char*)(d["location"]    | "");
  idleData.sunrise     = (const char*)(d["sunrise"]     | "");
  idleData.sunset      = (const char*)(d["sunset"]      | "");
  // hasWeather = true si le PC a vraiment de la meteo (pas juste des zeros)
  bool hwField = d["hasWeather"] | false;
  idleData.hasData     = hwField || idleData.weatherCode >= 0;

  // Parser le tableau news
  idleData.newsCount = 0;
  JsonArray arr = d["news"].as<JsonArray>();
  for (JsonVariant v : arr) {
    if (idleData.newsCount >= 10) break;
    idleData.news[idleData.newsCount++] = v.as<String>();
  }

  Serial.printf("[idle] temp=%.1f code=%d hasData=%d loc=%s news=%d\n",
    idleData.temp, idleData.weatherCode, (int)idleData.hasData,
    idleData.location.c_str(), idleData.newsCount);

  // Forcer le redessin de l'ecran idle si on est dessus
  if (cur == SCR_WAIT) {
    idleDrawn = false;
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

// ----------------------------- Ecran meteo plein format --------------------
void drawIdleWeatherFull() {
  tft.fillScreen(C_BG);
  // --- Header compact ---
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("METEO", SCR_W/2, 2, 2);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(idleData.location, SCR_W/2, 16, 1);
  tft.fillRect(20, 26, SCR_W-40, 1, C_ACCENT);

  if (!idleData.hasData) {
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Meteo indisponible", SCR_W/2, 110, 2);
  } else {
    // Temperature : font 6 a gauche, icone a droite
    char tempBuf[10];
    snprintf(tempBuf, sizeof(tempBuf), "%.0fC", idleData.temp);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString(tempBuf, 90, 28, 6);
    // Icone a droite de la temperature
    drawWeatherIconLarge(240, 56, idleData.weatherCode);
    // Description font 2 sous la temp
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString(fitText(idleData.description, 2, 180), 90, 80, 2);
    // Ligne ressenti + humidite
    char buf[48];
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_DIM, C_BG);
    snprintf(buf, sizeof(buf), "Res. %.0fC", idleData.feelsLike);
    tft.drawString(buf, 8, 96, 2);
    snprintf(buf, sizeof(buf), "Hum %.0f%%", idleData.humidity);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(buf, SCR_W-8, 96, 2);
    // Vent + UV
    tft.setTextDatum(TL_DATUM);
    snprintf(buf, sizeof(buf), "Vent %.0f km/h", idleData.windSpeed);
    tft.drawString(buf, 8, 112, 2);
    if (idleData.uvIndex >= 0) {
      snprintf(buf, sizeof(buf), "UV %.0f", idleData.uvIndex);
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(RGB565(180,120,220), C_BG);
      tft.drawString(buf, SCR_W-8, 112, 2);
    }
    // Lever / Coucher
    if (idleData.sunrise.length() && idleData.sunset.length()) {
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(RGB565(210,160,50), C_BG);
      snprintf(buf, sizeof(buf), "Lever %s  Coucher %s", idleData.sunrise.c_str(), idleData.sunset.c_str());
      tft.drawString(buf, SCR_W/2, 128, 2);
    }
  }

  // --- Previsions 7 jours ---
  tft.fillRect(0, 148, SCR_W, 1, C_TRACK);
  if (idleData.forecastDays > 0) {
    static const char* dayShort[] = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};
    int n    = idleData.forecastDays;
    int colW = SCR_W / n;  // 320/7 = 45px
    struct tm tnow; getLocalTime(&tnow, 0);
    tft.setTextDatum(TC_DATUM);
    for (int i = 0; i < n; i++) {
      int cx   = i * colW + colW / 2;
      int wday = (tnow.tm_wday + i) % 7;
      // Fond leger pour jour courant
      if (i == 0) tft.fillRect(i*colW, 150, colW-1, 89, RGB565(14,14,28));
      // Jour (abrege)
      tft.setTextColor(i == 0 ? C_ACCENT : C_DIM, C_BG);
      tft.drawString(dayShort[wday], cx, 154, 1);
      // Temperature max en grand, min en petit dessous
      char tb[8];
      snprintf(tb, sizeof(tb), "%.0f", idleData.forecast[i].tMax);
      tft.setTextColor(C_TEXT, C_BG);
      tft.drawString(tb, cx, 164, 2);
      snprintf(tb, sizeof(tb), "%.0f", idleData.forecast[i].tMin);
      tft.setTextColor(C_DIM, C_BG);
      tft.drawString(tb, cx, 182, 1);
      // Petit separateur vertical entre colonnes
      if (i > 0) tft.fillRect(i*colW-1, 150, 1, 89, RGB565(22,22,36));
    }
  } else {
    tft.setTextDatum(TC_DATUM); tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Previsions en attente...", SCR_W/2, 190, 1);
  }
  // Hint retour
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RGB565(45,45,65), C_BG);
  tft.drawString("< tap pour revenir", SCR_W/2, 239, 1);
}

// ----------------------------- Ecran news plein format ---------------------

void drawIdleNewsFull() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("ACTUALITES", SCR_W/2, 6, 4);
  tft.fillRect(0, 34, SCR_W, 1, C_TRACK);

  if (idleData.newsCount == 0) {
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Aucune actualite disponible", SCR_W/2, 120, 2);
  } else {
    int y = 40;
    for (int i = 0; i < idleData.newsCount && y < SCR_H - 18; i++) {
      // Numero
      char num[4]; snprintf(num, sizeof(num), "%d.", i+1);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_ACCENT, C_BG);
      tft.drawString(num, 4, y, 2);
      // Titre (tronque si trop long)
      tft.setTextColor(C_TEXT, C_BG);
      tft.drawString(fitText(idleData.news[i], 2, SCR_W - 26), 24, y, 2);
      y += 20;
      tft.fillRect(0, y-3, SCR_W, 1, RGB565(28,28,40));
    }
  }
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RGB565(50,50,70), C_BG);
  tft.drawString("Tap pour revenir", SCR_W/2, SCR_H-2, 1);
}

// ----------------------------- Menu systeme ----------------------------
void drawMenuScreen() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("NowPlaying Hub", SCR_W/2, 6, 2);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("v" FW_VERSION, SCR_W/2, 22, 1);
  tft.fillRect(20, 34, SCR_W-40, 1, C_TRACK);

  // IP + WiFi
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("IP: " + WiFi.localIP().toString(), 8, 40, 2);
  tft.drawString("Panel: http://" + WiFi.localIP().toString(), 8, 56, 1);
  tft.drawString(":8123  (depuis le PC)", 8, 65, 1);

  // Controle luminosite
  tft.fillRect(20, 80, SCR_W-40, 40, RGB565(18,18,30));
  tft.drawRoundRect(20, 80, SCR_W-40, 40, 6, C_TRACK);
  // Bouton -
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, RGB565(18,18,30));
  tft.drawString("-", 50, 100, 4);
  // Barre luminosite
  int barFill = map(cfg.brightness, 13, 255, 0, 200);
  tft.fillRect(75, 93, 170, 14, RGB565(25,25,40));
  tft.fillRect(75, 93, barFill, 14, C_ACCENT);
  char bpct[8]; snprintf(bpct, sizeof(bpct), "%d%%", cfg.brightness * 100 / 255);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(bpct, SCR_W/2, 110, 1);
  // Bouton +
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, RGB565(18,18,30));
  tft.drawString("+", SCR_W-50, 100, 4);

  // Bouton Update info
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("Mise a jour via le panel PC", SCR_W/2, 140, 1);

  // Bouton Reboot
  tft.fillRoundRect(90, 162, 140, 30, 8, RGB565(80,20,20));
  tft.setTextColor(C_TEXT, RGB565(80,20,20));
  tft.drawString("Redemarrer", SCR_W/2, 177, 2);

  // Retour
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RGB565(45,45,65), C_BG);
  tft.drawString("Tap ailleurs pour revenir", SCR_W/2, 238, 1);
}

// ----------------------------- Detail d'un jour meteo ------------------
void drawDayDetail(int day) {
  static const char* dayNames2[] = {"Dimanche","Lundi","Mardi","Mercredi","Jeudi","Vendredi","Samedi"};
  tft.fillScreen(C_BG);
  struct tm tnow; getLocalTime(&tnow, 0);
  int wday = (tnow.tm_wday + day) % 7;
  // Header
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString(day == 0 ? "Aujourd'hui" : dayNames2[wday], SCR_W/2, 6, 2);
  tft.fillRect(20, 26, SCR_W-40, 1, C_ACCENT);
  // Temperature max / min
  char buf[20];
  tft.setTextColor(C_TEXT, C_BG);
  snprintf(buf, sizeof(buf), "%.0fC", idleData.forecast[day].tMax);
  tft.drawString(buf, SCR_W/2 - 40, 40, 6);
  tft.setTextColor(C_DIM, C_BG);
  snprintf(buf, sizeof(buf), "%.0fC", idleData.forecast[day].tMin);
  tft.drawString(buf, SCR_W/2 + 60, 70, 4);
  // Icone + description
  drawWeatherIconLarge(SCR_W/2, 130, idleData.forecast[day].code);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(wmoDesc(idleData.forecast[day].code), SCR_W/2, 166, 2);
  // Hint
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RGB565(45,45,65), C_BG);
  tft.drawString("Tap: retour semaine  2x tap: idle", SCR_W/2, 238, 1);
}
// ----------------------------- Handler /api/idleconfig --------------------
void handleGetIdleConfig() {
  JsonDocument d;
  d["lat"]    = idleCfg.lat;
  d["lon"]    = idleCfg.lon;
  d["loc"]    = idleCfg.loc;
  d["rssUrl"] = idleCfg.rssUrl;
  d["tz"]     = idleCfg.tz;
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}
void handlePostIdleConfig() {
  JsonDocument d;
  if (!server.hasArg("plain") || deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false}"); return;
  }
  if (!d["lat"].isNull())    idleCfg.lat    = d["lat"].as<float>();
  if (!d["lon"].isNull())    idleCfg.lon    = d["lon"].as<float>();
  if (!d["loc"].isNull())    idleCfg.loc    = d["loc"].as<String>();
  if (!d["rssUrl"].isNull()) idleCfg.rssUrl = d["rssUrl"].as<String>();
  if (!d["tz"].isNull() && d["tz"].as<String>().length() > 2) {
    idleCfg.tz = d["tz"].as<String>();
    setenv("TZ", idleCfg.tz.c_str(), 1);
    tzset();
  }
  saveIdleConfig();
  // Force un re-fetch
  lastWxFetch  = 0;
  lastRssFetch = 0;
  firstFetchDone = false;
  handleGetIdleConfig();
}
static String htmlEsc(String t) {
  t.replace("&", "&amp;"); t.replace("<", "&lt;"); t.replace(">", "&gt;"); t.replace("\"", "&quot;");
  return t;
}

static const char PAGE_HEAD[] =
  "<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>Now Playing</title><style>body{font:16px system-ui,sans-serif;background:#0e0e14;color:#f2f2f6;margin:0;padding:20px;max-width:480px}"
  "h1{font-size:20px}h2{font-size:15px;margin-top:28px;color:#8d8da0}input,button{width:100%;box-sizing:border-box;padding:10px;margin:6px 0 12px;"
  "border-radius:8px;border:1px solid #333;background:#171722;color:#fff;font:inherit}button{background:#fa2d48;border:0;font-weight:600}"
  "td{padding:3px 14px 3px 0;color:#bbb}small{color:#8d8da0}</style></head><body>";

void handlePortalPage() {
  String h = PAGE_HEAD;
  h += "<h1>Connecter la carte au Wi-Fi</h1><form method=POST action=/wifi>";
  h += "<label>Reseau</label><input name=ssid list=nets required autocomplete=off><datalist id=nets>" + portalNets + "</datalist>";
  h += "<label>Mot de passe</label><input name=pass type=password autocomplete=off>";
  h += "<button>Enregistrer et redemarrer</button></form><small>Seuls les reseaux 2,4 GHz sont pris en charge.</small></body></html>";
  server.send(200, "text/html; charset=utf-8", h);
}

// Page de statut (http://nowplaying.local)
void handleRoot() {
  if (portalActive) { handlePortalPage(); return; }
  String h = PAGE_HEAD;
  h += "<h1>Now Playing</h1><table>";
  h += "<tr><td>Firmware</td><td>v" FW_VERSION "</td></tr>";
  h += "<tr><td>Wi-Fi</td><td>" + htmlEsc(WiFi.SSID()) + " (" + String(WiFi.RSSI()) + " dBm)</td></tr>";
  h += "<tr><td>Adresse</td><td>" + WiFi.localIP().toString() + " / " HOSTNAME ".local</td></tr>";
  h += "<tr><td>Allume depuis</td><td>" + String(millis() / 60000UL) + " min</td></tr>";
  h += "<tr><td>Memoire libre</td><td>" + String(ESP.getFreeHeap() / 1024) + " Ko</td></tr>";
  h += "<tr><td>Lecture</td><td>" + (hasTrack ? htmlEsc(np.state + " - " + np.title) : String("rien")) + "</td></tr></table>";
  h += "<h2>Actions (jeton requis)</h2>";
  h += "<form method=POST action=/api/reboot><input name=token type=password placeholder='Jeton (OTA_TOKEN)'><button>Redemarrer la carte</button></form>";
  h += "<form method=POST action=/api/wifireset><input name=token type=password placeholder='Jeton (OTA_TOKEN)'><button>Oublier le Wi-Fi</button></form>";
  h += "<small>Reglages complets : panel de l'appli PC.</small></body></html>";
  server.send(200, "text/html; charset=utf-8", h);
}

void handlePortalSave() {
  if (!portalActive) { server.send(403, "text/plain", "interdit"); return; }
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  ssid.trim();
  if (!ssid.length()) { server.send(400, "text/plain", "SSID vide"); return; }
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  server.send(200, "text/html; charset=utf-8", String(PAGE_HEAD) +
    "<h1>Enregistre</h1><p>La carte redemarre et se connecte a &laquo; " + htmlEsc(ssid) + " &raquo;.</p></body></html>");
  delay(800);
  ESP.restart();
}

void handleReboot() {
  if (server.arg("token") != OTA_TOKEN) { server.send(403, "text/plain", "jeton refuse"); return; }
  server.send(200, "text/html; charset=utf-8", String(PAGE_HEAD) + "<p>Redemarrage...</p></body></html>");
  delay(400);
  ESP.restart();
}

void handleWifiReset() {
  if (server.arg("token") != OTA_TOKEN) { server.send(403, "text/plain", "jeton refuse"); return; }
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  server.send(200, "text/html; charset=utf-8", String(PAGE_HEAD) + "<p>Wi-Fi oublie, la carte redemarre.</p></body></html>");
  delay(400);
  ESP.restart();
}

void handleNotFound() {
  if (portalActive) {            // portail captif : tout est redirige vers la page de config
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "not found");
}

// ----------------------------- Mise a jour OTA -----------------------------
bool otaDenied = false, otaOk = false;
size_t otaTotal = 0, otaWritten = 0;
int otaLastPct = -1;

void drawOtaScreen(int pct) {
  if (pct < 0) {
    tft.fillScreen(C_BG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString("Mise a jour du firmware", SCR_W / 2, 90, 4);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Ne pas debrancher la carte", SCR_W / 2, 130, 2);
    pct = 0;
  }
  tft.drawRoundRect(40, 160, 240, 14, 5, C_TRACK);
  tft.fillRoundRect(42, 162, (236 * pct) / 100, 10, 4, C_ACCENT);
}

void handleUpdateDone() {
  if (otaDenied) {
    server.send(403, "application/json", "{\"ok\":false,\"error\":\"token\"}");
    return;
  }
  bool ok = otaOk && !Update.hasError();
  server.send(ok ? 200 : 500, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"update\"}");
  if (ok) { delay(600); ESP.restart(); }
  cur = SCR_NONE;
}

void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaDenied = (server.arg("token") != OTA_TOKEN);
    otaOk = false; otaWritten = 0; otaLastPct = -1;
    otaTotal = (size_t)server.arg("size").toInt();
    if (otaDenied) return;
    drawOtaScreen(-1);
    if (!Update.begin(otaTotal > 0 ? otaTotal : UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaDenied) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    otaWritten += up.currentSize;
    int pct = otaTotal ? (int)((otaWritten * 100) / otaTotal) : 0;
    if (pct != otaLastPct && pct % 2 == 0) { otaLastPct = pct; drawOtaScreen(pct); }
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaDenied) return;
    otaOk = Update.end(true);
    if (!otaOk) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (!otaDenied) Update.abort();
  }
}

// ----------------------------- Reseau --------------------------------------
bool netWasUp = false, serverStarted = false;

// Retourne true si le canal est 2.4 GHz (1-13), false si 5 GHz (36+)
static bool is24GHz(int32_t ch) { return ch >= 1 && ch <= 13; }

// Scanne les reseaux et retourne le canal + BSSID du meilleur AP 2.4 GHz pour WIFI_SSID.
// Renvoie -1 si rien trouve.
static int scanFor24GHz(uint8_t* bssidOut) {
  int n = WiFi.scanNetworks(/*async*/false, /*show_hidden*/false);
  int bestRssi = -9999, bestCh = -1;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != wifiSsid) continue;
    int ch = WiFi.channel(i);
    if (!is24GHz(ch)) { Serial.printf("[scan] ignore %s ch%d (5 GHz)\n", WiFi.BSSIDstr(i).c_str(), ch); continue; }
    int rssi = WiFi.RSSI(i);
    Serial.printf("[scan] %s ch%d rssi=%d\n", WiFi.BSSIDstr(i).c_str(), ch, rssi);
    if (rssi > bestRssi) { bestRssi = rssi; bestCh = ch; memcpy(bssidOut, WiFi.BSSID(i), 6); }
  }
  WiFi.scanDelete();
  return bestCh;
}

void startPortal() {
  if (portalActive) return;
  Serial.println("[wifi] mode configuration (portail captif)");
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks(false, false);
  portalNets = "";
  for (int i = 0; i < n; i++) {
    if (!is24GHz(WiFi.channel(i))) continue;
    portalNets += "<option value=\"" + htmlEsc(WiFi.SSID(i)) + "\">";
  }
  WiFi.scanDelete();
  WiFi.softAP(PORTAL_SSID);
  dnsServer.start(53, "*", WiFi.softAPIP());
  if (!serverStarted) { server.begin(); serverStarted = true; }
  portalActive = true;
  cur = SCR_NONE;               // redessine l'ecran avec les instructions
}

void stopPortal() {
  if (!portalActive) return;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActive = false;
}

void wifiTask() {
  static uint32_t lastTry   = 0;
  static uint32_t lastNtpRetry = 0;
  bool up = (WiFi.status() == WL_CONNECTED);
  if (up && !netWasUp) {
    Serial.printf("[wifi] connecte, IP %s\n", WiFi.localIP().toString().c_str());
    wifiError = "";
    everConnected = true;
    stopPortal();
    MDNS.end();
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    if (!serverStarted) { server.begin(); serverStarted = true; }
    // Configurer NTP maintenant qu'on a le WiFi
    if (!ntpConfigured) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      setenv("TZ", idleCfg.tz.c_str(), 1);
      tzset();
      ntpConfigured = true;
      Serial.println("[ntp] configuration lancee");
    }
    cur = SCR_NONE;                        // rafraichit l'ecran (IP)
  }
  netWasUp = up;
  // Si l'erreur Wi-Fi a change, force un redessin de l'ecran d'attente
  if (!up && wifiError != lastShownWifiErr) {
    lastShownWifiErr = wifiError;
    cur = SCR_NONE;
  }
  // Retry NTP si pas encore sync (toutes les 10s)
  if (up && ntpConfigured && !ntpSynced && millis() - lastNtpRetry > 10000) {
    lastNtpRetry = millis();
    ensureNtp();
    if (ntpSynced) {
      Serial.println("[ntp] synchronise OK");
      // Force redessin pour afficher la vraie heure
      if (::cur == SCR_WAIT) idleDrawn = false;
    }
  }
  // Pas de Wi-Fi memorise, ou jamais connecte 90 s apres le demarrage -> portail de configuration
  if (!up && !portalActive && (wifiSsid.length() == 0 || (!everConnected && millis() > 90000UL))) startPortal();
  if (!up && wifiSsid.length() && millis() - lastTry > (portalActive ? 60000UL : 10000UL)) {
    lastTry = millis();
    Serial.println("[wifi] scan + reconnexion 2.4 GHz...");
    WiFi.disconnect();
    uint8_t bssid[6];
    int ch = scanFor24GHz(bssid);
    if (ch > 0) {
      Serial.printf("[wifi] connexion au BSSID 2.4 GHz, ch%d\n", ch);
      WiFi.begin(wifiSsid.c_str(), wifiPass.c_str(), ch, bssid);
    } else {
      Serial.println("[wifi] aucun AP 2.4 GHz trouve, essai sans BSSID");
      WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
    }
  }
}

// ----------------------------- setup / loop --------------------------------
void setup() {
  Serial.begin(115200);
  loadSettings();
  loadIdleConfig();
  loadWifi();
  if (cfg.brightness < 200) { cfg.brightness = 255; saveSettings(); }  // force max
  applyAccent();

  tft.init();
  tft.setRotation(ROTATION);
  tft.setSwapBytes(true);
  tft.fillScreen(C_BG);
  blInit();
  applyBrightness();

  touchSPI.begin(25, 39, 32, 33);          // CLK, MISO, MOSI, CS
  ts.begin(touchSPI);
  ts.setRotation(ROTATION);

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tftOutput);

  coverBuf = (uint8_t*)malloc(COVER_MAX_BYTES);
  iconBuf  = (uint8_t*)malloc(ICON_MAX_BYTES);
  titleSpr.createSprite(COL_W, TITLE_H);
  barSpr.createSprite(BAR_W, BAR_H);
  eqSpr.createSprite(100, 72);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/nowplaying", HTTP_POST, handleNowPlaying);
  server.on("/api/cover", HTTP_POST, handleCover);
  server.on("/api/icon", HTTP_POST, handleIcon);
  server.on("/api/config", HTTP_GET, sendConfig);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/api/idle", HTTP_POST, handleIdle);
  server.on("/api/idleconfig", HTTP_GET,  handleGetIdleConfig);
  server.on("/api/idleconfig", HTTP_POST, handlePostIdleConfig);
  server.on("/wifi", HTTP_POST, handlePortalSave);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/wifireset", HTTP_POST, handleWifiReset);
  server.onNotFound(handleNotFound);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  // Force le Wi-Fi 2.4 GHz uniquement (802.11 b/g/n ; exclut le 5 GHz 802.11ac/ax)
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

  // Capture la raison en cas d'echec de connexion pour l'afficher a l'ecran
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      uint8_t r = info.wifi_sta_disconnected.reason;
      Serial.printf("[wifi] deconnecte, raison=%d\n", r);
      if      (r == 201) wifiError = "SSID introuvable";
      else if (r == 202) wifiError = "Mot de passe incorrect";
      else if (r == 204) wifiError = "Echec handshake";
      else if (r == 200) wifiError = "Beacon timeout";
      else if (r ==  36) wifiError = "Rejet routeur (band steering?)";
      else if (r ==   6) wifiError = "Non authentifie (router?)";
      else               wifiError = "Err " + String(r);
    }
  });

  // Scan pour trouver le BSSID 2.4 GHz specifique et eviter le band steering
  if (wifiSsid.length() == 0) {
    startPortal();                       // premiere utilisation : portail de configuration
  } else {
    uint8_t bssid[6];
    int ch = scanFor24GHz(bssid);
    if (ch > 0) {
      Serial.printf("[wifi] demarrage sur BSSID 2.4 GHz ch%d\n", ch);
      WiFi.begin(wifiSsid.c_str(), wifiPass.c_str(), ch, bssid);
    } else {
      Serial.println("[wifi] demarrage sans BSSID specifique");
      WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
    }
  }
}

void loop() {
  wifiTask();
  if (portalActive) dnsServer.processNextRequest();
  tickNetFetch();
  if (serverStarted) server.handleClient();

  Screen want;
  if (WiFi.status() != WL_CONNECTED) {
    want = SCR_WIFI;
  } else {
    bool timedOut = !hasTrack || millis() - lastRx >= IDLE_TIMEOUT_MS;
    // Bascule idle si en pause depuis trop longtemps
    bool pauseIdle = hasTrack && np.state == "paused" && pausedSince > 0
                     && millis() - pausedSince >= PAUSE_TO_IDLE_MS;
    if (pauseIdle) forcedIdle = true;
    if      (timedOut || forcedIdle) want = SCR_WAIT;
    else                             want = SCR_PLAY;
  }

  // Ne pas ecraser les ecrans detail (meteo/news plein format)
  bool onDetailScreen = (cur == SCR_IDLE_WX || cur == SCR_IDLE_NEWS || cur == SCR_MENU);
  if (!onDetailScreen && want != cur) {
    cur = want;
    lastClockMin = -1;   // force le redessin de l'heure au changement d'ecran
    fullCover = false;   // quitter le mode fullCover si on change d'ecran
    idleDrawn = false;   // forcer redessin de l'ecran idle
    if (cur == SCR_PLAY) drawPlayScreen();
    else if (cur == SCR_WIFI) drawWaitScreen(true);
    // SCR_WAIT sera dessine par animateIdle()
  }

  // Mise a jour de l'heure dans le header chaque minute (ecran lecture)
  if (cur == SCR_PLAY && !fullCover) {
    struct tm tm;
    if (getLocalTime(&tm, 0) && tm.tm_min != lastClockMin) {
      lastClockMin = tm.tm_min;
      headerDirty = true;
    }
  }

  tickAutoDim();

  if (cur == SCR_PLAY) {
    handleTouch();
    renderPlay();
  } else if (cur == SCR_WAIT) {
    handleTouch();   // on garde le touch actif pour reset auto-dim
    animateIdle();
  } else if (cur == SCR_IDLE_WX || cur == SCR_IDLE_NEWS || cur == SCR_MENU) {
    handleTouch();   // tap = retour idle
  } else {
    animateWait();
  }
}
