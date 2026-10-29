// ============================================================================
//  Now Playing - ESP32-2432S028R (CYD) - firmware v1.6
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
//    POST /api/coverbig?id=X  JPEG 240x240 en base64 (pochette plein ecran, envoyee a la demande)
//    POST /api/pcstats     JSON {cpu,ram,disk,ramUsed,ramTotal,diskFree,down,up,uptime} (stats du PC)
//    POST /api/sdclear     vide le cache de pochettes de la carte SD
//    UDP  8124             spectre audio : 'V' 'Z' <n> <n octets 0-255>  (visualiseur + LED)
//    Reponse de /api/nowplaying : {ok,needCover,needIcon,needBig,viz,cmd?}  (cmd = prev|toggle|next|seek:<sec>)
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
#include <SD.h>
#include <WiFiUdp.h>
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
void drawForecastScreen();
static void ellipsizeOn(TFT_eSPI& g, const char* in, char* out, size_t cap, int maxW);
int  fcHit(int x, int y);   // colonne de prevision touchee (-1 si aucune)
void drawNewsScreen();
void buildNewsTicker();
void drawMenuScreen();
void drawDayDetailScreen(int day);
void drawIdlePcFull(bool first);


// ----------------------------- Couleurs / layout ---------------------------
#define RGB565(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
static uint16_t cAccent = RGB565(250, 45, 72);
#define C_ACCENT cAccent
static const uint16_t C_BG    = 0x0000;              // noir pur #000000
static const uint16_t C_TEXT  = 0xFFFF;
static const uint16_t C_DIM   = RGB565(142, 142, 147);   // #8E8E93
static const uint16_t C_TRACK = RGB565(58, 58, 60);      // #3A3A3C
static const uint16_t C_PLACE = RGB565(28, 28, 30);      // #1C1C1E
static const uint16_t C_BTN   = RGB565(44, 44, 46);      // #2C2C2E

static const int SCR_W = 320, SCR_H = 240;
static const int COVER_X = 10, COVER_Y = 34, COVER_SZ = 120;
static const int COL_X = 142, COL_W = 168;           // colonne de droite
static const int TITLE_Y = 40, TITLE_H = 30;
static const int ARTIST_Y = 80, ALBUM_Y = 102;
static const int BTN_Y = 130, BTN_H = 36, BTN_W = 52, BTN_STEP = 58;  // 3 boutons
static const int BAR_X = 10, BAR_Y = 166, BAR_W = 300, BAR_H = 40;

// ----------------------------- Reglages persistants ------------------------
struct Settings {
  uint8_t  briPct     = 100;  // luminosite 5-100 %
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
  // --- v1.6 ---
  bool     autoBri    = false; // luminosite automatique (capteur de lumiere)
  int16_t  ldrDark    = 2600;  // calibration du capteur : valeur brute (ADC) en pleine obscurite
  int16_t  ldrBright  = 300;   //                         ... et en piece bien eclairee
  uint8_t  ledMode    = 0;     // LED RGB : 0 eteinte, 1 couleur d'accent, 2 pulsation, 3 suit les basses
  uint8_t  ledBri     = 40;    // luminosite de la LED en %
  bool     ledNightOff = true; // LED eteinte pendant le mode nuit
  bool     sdCache    = false; // cache de pochettes sur carte SD
  uint16_t sdMax      = 150;   // nombre de pistes gardees dans le cache
} cfg;
Preferences prefs;

void loadSettings() {
  prefs.begin("np", true);
  cfg.briPct     = prefs.getUChar("bPc", 100);
  cfg.accent     = prefs.getUInt("acc", 0xFA2D48);
  cfg.scroll     = prefs.getUShort("scr", 45);
  cfg.showApp    = prefs.getBool("app", true);
  cfg.nightOn    = prefs.getBool("nOn", true);
  cfg.nightStart = prefs.getUChar("nSt", 22);
  cfg.nightEnd   = prefs.getUChar("nEn", 7);
  cfg.nightBri   = prefs.getUChar("nBr", 8);
  cfg.dimMin     = prefs.getUChar("dMi", 3);
  cfg.dimBri     = prefs.getUChar("dBr", 15);
  cfg.autoBri    = prefs.getBool("aBr", false);
  cfg.ldrDark    = prefs.getShort("ldD", 2600);
  cfg.ldrBright  = prefs.getShort("ldB", 300);
  cfg.ledMode    = prefs.getUChar("lMo", 0);
  cfg.ledBri     = prefs.getUChar("lBr", 40);
  cfg.ledNightOff = prefs.getBool("lNo", true);
  cfg.sdCache    = prefs.getBool("sdC", false);
  cfg.sdMax      = prefs.getUShort("sdM", 150);
  prefs.end();
}
void saveSettings() {
  prefs.begin("np", false);
  prefs.putUChar("bPc", cfg.briPct);
  prefs.putUInt("acc", cfg.accent);
  prefs.putUShort("scr", cfg.scroll);
  prefs.putBool("app", cfg.showApp);
  prefs.putBool("nOn", cfg.nightOn);
  prefs.putUChar("nSt", cfg.nightStart);
  prefs.putUChar("nEn", cfg.nightEnd);
  prefs.putUChar("nBr", cfg.nightBri);
  prefs.putUChar("dMi", cfg.dimMin);
  prefs.putUChar("dBr", cfg.dimBri);
  prefs.putBool("aBr", cfg.autoBri);
  prefs.putShort("ldD", cfg.ldrDark);
  prefs.putShort("ldB", cfg.ldrBright);
  prefs.putUChar("lMo", cfg.ledMode);
  prefs.putUChar("lBr", cfg.ledBri);
  prefs.putBool("lNo", cfg.ledNightOff);
  prefs.putBool("sdC", cfg.sdCache);
  prefs.putUShort("sdM", cfg.sdMax);
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
static uint8_t pctToPwm(int pct) { return (uint8_t)constrain(pct * 255 / 100, 13, 255); }
void applyBrightness() {
  blWrite(pctToPwm(cfg.briPct));
}

// Capteur de lumiere (LDR) du CYD : GPIO 34. La calibration (obscurite / lumiere) se fait depuis le panel,
// ce qui marche quel que soit le sens du capteur (valeur qui monte ou qui descend avec la lumiere).
static const int LDR_PIN = 34;
static int   ldrRaw = 0;
static float ldrAvg = -1;
static void tickLdr() {
  static uint32_t t = 0;
  if (millis() - t < 150) return;
  t = millis();
  ldrRaw = analogRead(LDR_PIN);
  ldrAvg = (ldrAvg < 0) ? (float)ldrRaw : ldrAvg * 0.85f + ldrRaw * 0.15f;
}
// 0.0 = sombre ... 1.0 = lumineux
static float ldrLight() {
  if (ldrAvg < 0) return 1.0f;
  float d = (float)cfg.ldrDark, b = (float)cfg.ldrBright;
  if (fabsf(b - d) < 50.0f) return 1.0f;
  return constrain((ldrAvg - d) / (b - d), 0.0f, 1.0f);
}

// LED RGB du CYD : GPIO 4 (R), 16 (G), 17 (B), allumee a l'etat bas
static const int LED_R = 4, LED_G = 16, LED_B = 17;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  static void ledInit() { ledcAttach(LED_R, 5000, 8); ledcAttach(LED_G, 5000, 8); ledcAttach(LED_B, 5000, 8); }
  static void ledPwm(uint8_t r, uint8_t g, uint8_t b) { ledcWrite(LED_R, 255 - r); ledcWrite(LED_G, 255 - g); ledcWrite(LED_B, 255 - b); }
#else
  static void ledInit() {
    ledcSetup(1, 5000, 8); ledcSetup(2, 5000, 8); ledcSetup(3, 5000, 8);
    ledcAttachPin(LED_R, 1); ledcAttachPin(LED_G, 2); ledcAttachPin(LED_B, 3);
  }
  static void ledPwm(uint8_t r, uint8_t g, uint8_t b) { ledcWrite(1, 255 - r); ledcWrite(2, 255 - g); ledcWrite(3, 255 - b); }
#endif

// ----------------------------- Objets globaux -----------------------------
TFT_eSPI    tft;
TFT_eSprite titleSpr(&tft);
TFT_eSprite barSpr(&tft);
TFT_eSprite eqSpr(&tft);
#include "Utf8Ascii.h"         // cleanUTF8() : accents -> ASCII
#include "ModernDashboard.h"   // v1.7 : ecran de veille moderne (cartes + sprites)
WebServer   server(80);
SPIClass    touchSPI(VSPI);
XPT2046_Touchscreen ts(33, 36);   // CS, IRQ

// ----------------------------- Etat lecture --------------------------------
struct Track {
  String title, artist, album, state = "stopped", coverId, app, appId;
  float  duration = 0;
  String lyric, lyricPrev, lyricNext;
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

enum Screen { SCR_NONE, SCR_WIFI, SCR_WAIT, SCR_PLAY, SCR_IDLE_WX, SCR_IDLE_NEWS, SCR_MENU, SCR_IDLE_PC };
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

// ----------------------------- v1.6 : modes d'affichage, pochette HD, spectre, stats PC, LED
bool     vizMode   = false;     // ecran visualiseur de spectre
bool     lyricFull = false;     // paroles en grand
bool     bigDirty  = false;     // pochette plein ecran a redessiner
uint8_t* bigBuf    = nullptr;   // pochette 240x240
size_t   bigLen    = 0;
String   bigIdStored;
static bool gNight = false;     // mode nuit actif (calcule par tickAutoDim)

struct PcStats {
  bool valid = false; uint32_t ms = 0;
  uint8_t cpu = 0, ram = 0, disk = 0;
  float ramUsed = 0, ramTotal = 0, diskFree = 0;     // Go
  uint32_t down = 0, up = 0;                         // Ko/s
  uint32_t uptimeMin = 0;
} pcs;
static bool pcLineDirty = false, pcPageDirty = false;
static bool pcsFresh() { return pcs.valid && millis() - pcs.ms < 20000; }

static const uint16_t VIZ_PORT = 8124;
static const int VIZ_MAX = 32;
WiFiUDP  vizUdp;
bool     vizUdpOn = false;
static uint8_t  vizLvl[VIZ_MAX];
static int      vizN = 0;
static uint32_t vizRxMs = 0, vizPkts = 0;
static float    vizBass = 0;

static void pollViz() {
  if (!vizUdpOn) return;
  for (int guard = 0; guard < 6 && vizUdp.parsePacket() > 0; guard++) {
    uint8_t buf[VIZ_MAX + 4];
    int n = vizUdp.read(buf, sizeof(buf));
    if (n < 4 || buf[0] != 'V' || buf[1] != 'Z') continue;
    int cnt = min((int)buf[2], min(VIZ_MAX, n - 3));
    if (cnt < 3) continue;
    memcpy(vizLvl, buf + 3, cnt);
    vizN = cnt;
    vizRxMs = millis();
    vizPkts++;
    float b = (vizLvl[0] + vizLvl[1] + vizLvl[2]) / (3.0f * 255.0f);
    vizBass = max(b, vizBass * 0.8f);
  }
}
// Le PC n'envoie le spectre que si la carte le demande (reponse a /api/nowplaying)
static bool wantViz() { return cur == SCR_PLAY && np.state == "playing" && (vizMode || cfg.ledMode == 3); }

static void tickLed() {
  static uint32_t t = 0;
  if (millis() - t < 30) return;
  t = millis();
  float k = 0;
  bool show = cfg.ledMode != 0 && cur == SCR_PLAY && !(cfg.ledNightOff && gNight);
  if (show) {
    float breath = 0.45f + 0.55f * (0.5f + 0.5f * sinf(millis() / 1000.0f * 2.2f));
    if (np.state != "playing")      k = 0.15f;
    else if (cfg.ledMode == 1)      k = 1.0f;
    else if (cfg.ledMode == 2)      k = breath;
    else {
      bool live = vizN > 0 && millis() - vizRxMs < 600;
      k = live ? constrain(0.08f + vizBass, 0.0f, 1.0f) : breath;
      vizBass *= 0.9f;
    }
  }
  uint32_t a = dynAccent ? dynAccent : cfg.accent;
  float s = powf(k * cfg.ledBri / 100.0f, 1.8f);
  ledPwm((uint8_t)(((a >> 16) & 0xFF) * s), (uint8_t)(((a >> 8) & 0xFF) * s), (uint8_t)((a & 0xFF) * s));
}

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
  String loc    = "Bruxelles";
  String rssUrl = "https://news.google.com/rss?hl=fr&gl=FR&ceid=FR:fr";
  String tz     = "CET-1CEST,M3.5.0,M10.5.0/3";   // fuseau POSIX
} idleCfg;

void loadIdleConfig() {
  prefs.begin("idle", true);
  idleCfg.lat    = prefs.getFloat("ilat",  50.595f);
  idleCfg.lon    = prefs.getFloat("ilon",  5.117f);
  idleCfg.loc    = prefs.getString("iloc", "Bruxelles");
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
static const int IDLE_NEWS_H   = 36;   // zone tactile du bandeau d'actus
static const int IDLE_TOPBAR_Y = 22;   // WiFi bars + hint
static const int IDLE_CLK_Y    = 30;   // horloge
static const int IDLE_CLK_FONT = 7;
static const int IDLE_DATE_Y   = 104;  // date
static const int IDLE_SEP_Y    = 122;  // separateur accent
static const int IDLE_WX_Y     = 42;   // cartes heure/meteo (zone tactile)
static const int IDLE_WX_H     = 188;
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

// Coupe s en lignes (aux espaces) ; la derniere ligne est tronquee avec "..." si ca depasse encore
static int wrapText(const String& s, int font, int maxW, int maxLines, String* out) {
  String rest = s;
  rest.trim();
  int n = 0;
  while (rest.length() && n < maxLines) {
    if (tft.textWidth(rest, font) <= maxW) { out[n++] = rest; break; }
    if (n == maxLines - 1) { out[n++] = fitText(rest, font, maxW); break; }
    int cut = -1;                                   // dernier espace qui tient sur la ligne
    for (int i = 0; i < (int)rest.length(); i++) {
      if (rest[i] != ' ') continue;
      if (tft.textWidth(rest.substring(0, i), font) > maxW) break;
      cut = i;
    }
    if (cut <= 0) {                                 // mot trop long : coupe brutale
      cut = rest.length();
      while (cut > 1 && tft.textWidth(rest.substring(0, cut), font) > maxW) cut--;
      out[n++] = rest.substring(0, cut);
      rest = rest.substring(cut);
    } else {
      out[n++] = rest.substring(0, cut);
      rest = rest.substring(cut + 1);
    }
    rest.trim();
  }
  return n;
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


// ----------------------------- Cache de pochettes sur carte SD (optionnel) --
// Le lecteur SD du CYD est sur le bus VSPI (CS 5, SCK 18, MISO 19, MOSI 23), le meme peripherique que le
// tactile (CLK 25, MISO 39, MOSI 32). Les deux sont donc "re-attaches" avant chaque acces a l'un ou l'autre.
static const int SD_CS = 5, SD_SCK = 18, SD_MISO = 19, SD_MOSI = 23;
static SPIClass sdSPI(VSPI);
static bool     sdOk = false, sdBusDirty = false;
static uint32_t sdCount = 0;
static uint32_t sdSizeMB = 0;
static const char* SD_DIR_S = "/np_s";    // petites pochettes (120x120)
static const char* SD_DIR_B = "/np_b";    // grandes pochettes (240x240)

static void sdBus()    { sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS); sdBusDirty = true; }
static void touchBus() { if (sdBusDirty) { touchSPI.begin(25, 39, 32, 33); sdBusDirty = false; } }

static void sdMount() {
  sdOk = false;
  sdBus();
  if (!SD.begin(SD_CS, sdSPI, 20000000) || SD.cardType() == CARD_NONE) {
    Serial.println("[sd] pas de carte");
    return;
  }
  sdOk = true;
  sdSizeMB = (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));
  SD.mkdir(SD_DIR_S);
  SD.mkdir(SD_DIR_B);
  sdCount = 0;
  const char* dirs[2] = { SD_DIR_S, SD_DIR_B };
  for (int k = 0; k < 2; k++) {
    File d = SD.open(dirs[k]);
    if (!d) continue;
    File f = d.openNextFile();
    while (f) { sdCount++; f.close(); f = d.openNextFile(); }
    d.close();
  }
  Serial.printf("[sd] carte OK : %u Mo, %u fichiers en cache\n", (unsigned)sdSizeMB, (unsigned)sdCount);
}

static String sdName(const char* dir, const String& id) {
  String o;
  for (size_t i = 0; i < id.length() && o.length() < 24; i++) {
    char c = id[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_') o += c;
  }
  if (!o.length()) return "";
  return String(dir) + "/" + o + ".jpg";
}

static bool sdLoad(const char* dir, const String& id, uint8_t* buf, size_t cap, size_t& outLen) {
  if (!sdOk || !cfg.sdCache || !buf) return false;
  String p = sdName(dir, id);
  if (!p.length()) return false;
  sdBus();
  File f = SD.open(p, FILE_READ);
  if (!f) return false;
  size_t n = f.size();
  bool ok = n > 4 && n <= cap && f.read(buf, n) == n && buf[0] == 0xFF && buf[1] == 0xD8;
  f.close();
  if (ok) outLen = n;
  return ok;
}

// Quand le cache est plein : supprime les premiers fichiers (la FAT les range a peu pres par ordre de creation)
static void sdPrune() {
  uint32_t target = (uint32_t)cfg.sdMax * 2 * 9 / 10;
  const char* dirs[2] = { SD_DIR_S, SD_DIR_B };
  while (sdCount > target) {
    String victims[24];
    int got = 0;
    for (int k = 0; k < 2 && got < 24; k++) {
      File d = SD.open(dirs[k]);
      if (!d) continue;
      File f = d.openNextFile();
      while (f && got < 24) { victims[got++] = String(f.path()); f.close(); f = d.openNextFile(); }
      if (f) f.close();
      d.close();
    }
    if (!got) break;
    bool any = false;
    for (int i = 0; i < got; i++) if (SD.remove(victims[i])) { any = true; if (sdCount) sdCount--; }
    if (!any) break;
  }
}

static void sdSave(const char* dir, const String& id, const uint8_t* buf, size_t len) {
  if (!sdOk || !cfg.sdCache || !buf || len == 0) return;
  String p = sdName(dir, id);
  if (!p.length()) return;
  sdBus();
  if (SD.exists(p)) return;
  File f = SD.open(p, FILE_WRITE);
  if (!f) return;
  size_t w = f.write(buf, len);
  f.close();
  if (w != len) { SD.remove(p); return; }
  sdCount++;
  if (sdCount > (uint32_t)cfg.sdMax * 2) sdPrune();
}

static void sdClear() {
  if (!sdOk) return;
  sdBus();
  const char* dirs[2] = { SD_DIR_S, SD_DIR_B };
  for (int pass = 0; pass < 40; pass++) {             // 40 x 24 fichiers max par appel
    String victims[24];
    int got = 0;
    for (int k = 0; k < 2 && got < 24; k++) {
      File d = SD.open(dirs[k]);
      if (!d) continue;
      File f = d.openNextFile();
      while (f && got < 24) { victims[got++] = String(f.path()); f.close(); f = d.openNextFile(); }
      if (f) f.close();
      d.close();
    }
    if (!got) break;
    for (int i = 0; i < got; i++) SD.remove(victims[i]);
  }
  sdCount = 0;
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

// Ligne tres discrete (stats du PC) en bas a gauche de l'ecran de veille ; un tap dessus ouvre la page "Mon PC"
void drawIdlePcLine() {
  tft.fillRect(0, 230, 215, 10, C_BG);
  if (!pcsFresh()) return;
  char b[48];
  snprintf(b, sizeof(b), "PC  cpu %u%%  ram %u%%  dsk %u%%", pcs.cpu, pcs.ram, pcs.disk);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(RGB565(72, 78, 105), C_BG);
  tft.drawString(b, 4, 232, 1);
}
static void tickIdlePcLine() {
  static bool shown = false;
  bool f = pcsFresh();
  if (pcLineDirty || f != shown) { pcLineDirty = false; shown = f; drawIdlePcLine(); }
}

// ---- Ecran de veille v1.7 : voir ModernDashboard.h ------------------------
// Remplit la structure d'affichage a partir de l'heure NTP et de idleData (aucune String creee ici)
static void fillDashData(ui::DashboardData& d) {
  struct tm tm;
  d.timeValid = getLocalTime(&tm, 0) && tm.tm_year > 100;
  if (d.timeValid) {
    d.hour = tm.tm_hour; d.minute = tm.tm_min; d.second = tm.tm_sec;
    d.wday = tm.tm_wday; d.mday = tm.tm_mday; d.month = tm.tm_mon + 1;
  }
  ui::WeatherData& w = d.wx;
  w.valid = idleData.hasData;
  if (w.valid) {
    w.tempC    = (int8_t)lroundf(idleData.temp);
    w.feelsC   = (int8_t)lroundf(idleData.feelsLike);
    w.windKmh  = (uint8_t)constrain((int)lroundf(idleData.windSpeed), 0, 255);
    w.humidity = (uint8_t)constrain((int)lroundf(idleData.humidity), 0, 100);
    w.icon     = ui::wmoToIcon(idleData.weatherCode);
    strlcpy(w.label, idleData.description.c_str(), sizeof(w.label));
  }
  // Bandeau : actus si dispo, sinon le nom du lieu
  d.ticker = (idleData.newsCount > 0 && newsFullStr.length()) ? newsFullStr.c_str()
                                                              : idleData.location.c_str();
}

void drawHomeScreen() {
  static ui::DashboardData d;
  fillDashData(d);
  g_dashboard.invalidate();              // fond + cartes redessines
  drawModernDashboard(d);
  tft.setTextFont(1);   // les free fonts restent actives sinon : tout drawString(...,1) du firmware serait faux
  // Elements discrets sous les cartes (y >= 230)
  tft.fillRect(215, 230, 105, 10, C_BG);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(RGB565(38,38,52), C_BG);
  tft.drawString(WiFi.localIP().toString(), SCR_W-4, IDLE_IP_Y, 1);
  drawIdlePcLine();
  if (idleData.newsCount > 0) buildNewsTicker();
  idleDrawn    = true;
  lastWifiDraw = 0;
}

void animateIdle() {
  if (!idleDrawn) { drawHomeScreen(); return; }
  static ui::DashboardData d;
  static uint32_t lastFill = 0;
  uint32_t now = millis();
  if (now - lastFill >= 100) { lastFill = now; fillDashData(d); }   // heure/meteo : 10x/s suffit
  drawModernDashboard(d);                                           // sprites : ne redessine que ce qui change
  tickIdlePcLine();
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

// Boutons : conteneurs #2C2C2E arrondis + icones blanches en primitives (triangles / barres arrondies)
void drawButtons(int hi = -1) {
  for (int i = 0; i < 3; i++) {
    const int x = COL_X + i * BTN_STEP, cx = x + BTN_W / 2, cy = BTN_Y + BTN_H / 2;
    const uint16_t bg = (i == hi) ? C_ACCENT : C_BTN;
    tft.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 10, bg);
    if (i == 0) {                         // precedent : 2 triangles vers la gauche
      tft.fillTriangle(cx - 1, cy - 8, cx - 1, cy + 8, cx - 11, cy, C_TEXT);
      tft.fillTriangle(cx + 10, cy - 8, cx + 10, cy + 8, cx, cy, C_TEXT);
    } else if (i == 1) {                  // pause si lecture, sinon play
      if (np.state == "playing") {
        tft.fillRoundRect(cx - 8, cy - 9, 6, 18, 2, C_TEXT);
        tft.fillRoundRect(cx + 2, cy - 9, 6, 18, 2, C_TEXT);
      } else {
        tft.fillTriangle(cx - 6, cy - 10, cx - 6, cy + 10, cx + 9, cy, C_TEXT);
      }
    } else {                              // suivant : 2 triangles vers la droite
      tft.fillTriangle(cx - 10, cy - 8, cx - 10, cy + 8, cx, cy, C_TEXT);
      tft.fillTriangle(cx + 1, cy - 8, cx + 1, cy + 8, cx + 11, cy, C_TEXT);
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

// Pochette plein ecran : 240x240 (envoyee a la demande par le PC), sinon la petite pochette centree en attendant
void drawFullCoverScreen() {
  tft.fillScreen(C_BG);
  bool bigOk = bigBuf && bigLen > 0 && np.coverId.length() && bigIdStored == np.coverId;
  if (bigOk) {
    TJpgDec.drawJpg((SCR_W - 240) / 2, 0, bigBuf, bigLen);
    return;
  }
  bool ok = coverLen > 0 && np.coverId.length() && coverIdStored == np.coverId;
  int cx = (SCR_W - COVER_SZ) / 2;
  int cy = (SCR_H - COVER_SZ) / 2;
  if (ok) {
    TJpgDec.drawJpg(cx, cy, coverBuf, coverLen);
  } else {
    tft.fillRoundRect(cx, cy, COVER_SZ, COVER_SZ, 10, C_PLACE);
    int icx = cx + COVER_SZ / 2, icy = cy + COVER_SZ / 2;
    tft.fillCircle(icx - 10, icy + 22, 12, C_DIM);
    tft.fillRect(icx + 1, icy - 32, 5, 54, C_DIM);
    tft.fillTriangle(icx + 6, icy - 32, icx + 28, icy - 20, icx + 6, icy - 8, C_DIM);
  }
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RGB565(60, 60, 75), C_BG);
  tft.drawString("Chargement HD...", SCR_W / 2, SCR_H - 4, 2);
}

void drawArtistAlbum() {
  char line[96];
  tft.fillRect(COL_X, ARTIST_Y - 2, COL_W, 48, C_BG);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM);
  ellipsizeOn(tft, np.artist.c_str(), line, sizeof(line), COL_W);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(line, COL_X, ARTIST_Y + 9);
  ellipsizeOn(tft, np.album.c_str(), line, sizeof(line), COL_W);
  tft.setTextColor(RGB565(99, 99, 102), C_BG);                     // #636366
  tft.drawString(line, COL_X, ALBUM_Y + 9);
  tft.setTextFont(1);                                              // rend la police GLCD au reste du firmware
}

// TITLE_SCROLL 1 : defilement fluide si le titre depasse (clippe par le sprite, ne touche jamais l'heure)
//               0 : titre fixe tronque avec "..."
#ifndef TITLE_SCROLL
#define TITLE_SCROLL 1
#endif

void renderTitleSprite(int off) {
  titleSpr.fillSprite(C_BG);
  titleSpr.setFreeFont(&FreeSansBold12pt7b);
  titleSpr.setTextDatum(TL_DATUM);
  titleSpr.setTextColor(C_TEXT, C_BG);
  if (!TITLE_SCROLL && titleW > COL_W) {
    char line[96];
    ellipsizeOn(titleSpr, np.title.c_str(), line, sizeof(line), COL_W);
    titleSpr.drawString(line, 0, 2);
  } else {
    titleSpr.drawString(np.title.c_str(), -off, 2);
    if (titleW > COL_W) titleSpr.drawString(np.title.c_str(), -off + titleW + 40, 2);
  }
  titleSpr.pushSprite(COL_X, TITLE_Y);
}

void layoutTitle() {
  titleSpr.setFreeFont(&FreeSansBold12pt7b);
  titleW = titleSpr.textWidth(np.title.c_str());
  titleStaticDrawn = false;
  scrollStart = millis();
  lastOff = -9999;
}

void tickTitle() {
  if (!TITLE_SCROLL || titleW <= COL_W) {
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
  // Barre fine (3 px) : piste #3A3A3C, partie lue blanche, petit curseur rond
  barSpr.fillRoundRect(0, 6, BAR_W, 3, 1, C_TRACK);
  int fw = (np.duration > 0) ? (int)(BAR_W * pos / np.duration) : 0;
  if (fw > BAR_W) fw = BAR_W;
  if (fw >= 3)     barSpr.fillRoundRect(0, 6, fw, 3, 1, C_TEXT);
  else if (fw > 0) barSpr.fillRect(0, 6, fw, 3, C_TEXT);
  barSpr.fillSmoothCircle(constrain(fw, 4, BAR_W - 4), 7, 4, C_TEXT, C_BG);

  char a[12], b[12];
  fmtTime(sec, a, sizeof(a));
  if (np.duration > 0) fmtTime((int)np.duration, b, sizeof(b)); else strcpy(b, "--:--");
  barSpr.setTextColor(C_DIM, C_BG);
  barSpr.setTextDatum(TL_DATUM); barSpr.drawString(a, 0, 20, 2);
  barSpr.setTextDatum(TR_DATUM); barSpr.drawString(b, BAR_W, 20, 2);
  barSpr.pushSprite(BAR_X, BAR_Y);
}

// Paroles : jusqu'a 2 lignes centrees, zone sous la barre de progression
void drawLyric() {
  const int LY_TOP = BAR_Y + BAR_H;          // 206
  const int LY_H   = SCR_H - LY_TOP;         // 34 px = 2 lignes de police 2
  const int LINE_H = 16;
  tft.fillRect(0, LY_TOP, SCR_W, LY_H, C_BG);
  if (!np.lyric.length()) return;
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  String l[2];
  int n = wrapText(np.lyric, 2, SCR_W - 8, 2, l);
  if (n >= 2) {
    tft.drawString(l[0], SCR_W / 2, LY_TOP + 1, 2);
    tft.drawString(l[1], SCR_W / 2, LY_TOP + 1 + LINE_H, 2);
  } else if (n == 1) {
    tft.drawString(l[0], SCR_W / 2, LY_TOP + (LY_H - LINE_H) / 2, 2);   // 1 ligne : centree verticalement
  }
}

// ----------------------------- Paroles plein ecran --------------------------
void drawLyricFull() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(fitText(np.title + " - " + np.artist, 1, SCR_W - 16), SCR_W / 2, 4, 1);

  String l[4];
  // ligne precedente (en haut, grisee, 2 lignes max, alignee sur le bas de sa zone)
  int pn = wrapText(np.lyricPrev, 2, SCR_W - 20, 2, l);
  for (int i = 0; i < pn; i++) tft.drawString(l[i], SCR_W / 2, 56 - (pn - i) * 16, 2);

  // ligne en cours (grande, 4 lignes max, centree verticalement)
  tft.setTextColor(C_TEXT, C_BG);
  int n = wrapText(np.lyric.length() ? np.lyric : String("..."), 4, SCR_W - 16, 4, l);
  int y0 = 62 + (112 - n * 26) / 2;
  for (int i = 0; i < n; i++) tft.drawString(l[i], SCR_W / 2, y0 + i * 26, 4);

  // ligne suivante (en bas, grisee)
  tft.setTextColor(C_DIM, C_BG);
  int nn = wrapText(np.lyricNext, 2, SCR_W - 20, 2, l);
  for (int i = 0; i < nn; i++) tft.drawString(l[i], SCR_W / 2, 182 + i * 16, 2);
}

// fine barre de progression en bas de l'ecran des paroles
void tickLyricBar() {
  static int lastS = -1;
  int s = (int)curPos();
  if (s == lastS) return;
  lastS = s;
  const int y = SCR_H - 3;
  tft.fillRect(0, y, SCR_W, 3, C_TRACK);
  if (np.duration > 0) tft.fillRect(0, y, (int)(SCR_W * curPos() / np.duration), 3, C_ACCENT);
}

void renderLyricFull() {
  if (trackDirty) { trackDirty = false; coverDirty = false; lyricDirty = true; }   // nouveau titre : tout redessiner
  if (lyricDirty) { lyricDirty = false; drawLyricFull(); }
  tickLyricBar();
}

// ----------------------------- Visualiseur de spectre -----------------------
static const int VZ_X0 = 10, VZ_W = 300, VZ_BASE = 214, VZ_H = 160;
static int      vzH[VIZ_MAX], vzPeak[VIZ_MAX];
static uint32_t vzPeakMs[VIZ_MAX];
static bool     vzMsgShown = false;

static void drawVizHeader() {
  tft.fillRect(0, 0, SCR_W, 40, C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(fitText(np.title, 2, SCR_W - 20), 10, 4, 2);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(fitText(np.artist, 2, SCR_W - 20), 10, 21, 2);
}

void drawVizScreen() {
  tft.fillScreen(C_BG);
  drawVizHeader();
  tft.fillRect(VZ_X0, VZ_BASE + 2, VZ_W, 1, C_TRACK);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(RGB565(60, 60, 75), C_BG);
  tft.drawString("Tap pour revenir", SCR_W / 2, SCR_H - 3, 1);
  for (int i = 0; i < VIZ_MAX; i++) { vzH[i] = 0; vzPeak[i] = 0; vzPeakMs[i] = 0; }
  vzMsgShown = false;
}

void renderViz() {
  if (trackDirty) { trackDirty = false; coverDirty = false; drawVizHeader(); }
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw < 33) return;
  lastDraw = millis();

  bool live = vizN > 0 && millis() - vizRxMs < 1500;
  int n = (vizN > 0) ? vizN : 24;
  int bw = VZ_W / n;
  bool allZero = true;
  for (int i = 0; i < n; i++) {
    int target = live ? (int)vizLvl[i] * VZ_H / 255 : 0;
    int h = vzH[i];
    if (target >= h) h = target;                                // montee immediate
    else             h = max(target, h - max(3, (h - target) / 4));   // descente progressive
    if (h > 0) allZero = false;

    int oldPeak = vzPeak[i], peak = oldPeak;
    if (h >= peak) { peak = h; vzPeakMs[i] = millis(); }
    else if (millis() - vzPeakMs[i] > 350) peak = max(h, peak - 2);

    if (h == vzH[i] && peak == oldPeak) continue;
    int x = VZ_X0 + i * bw, w = max(1, bw - 2);
    if (oldPeak > 0) tft.fillRect(x, VZ_BASE - oldPeak - 3, w, 3, C_BG);        // efface l'ancien chapeau
    if (h > vzH[i])      tft.fillRect(x, VZ_BASE - h, w, h - vzH[i], C_ACCENT);
    else if (h < vzH[i]) tft.fillRect(x, VZ_BASE - vzH[i], w, vzH[i] - h, C_BG);
    if (peak > 0) tft.fillRect(x, VZ_BASE - peak - 3, w, 3, C_TEXT);            // chapeau
    vzH[i] = h; vzPeak[i] = peak;
  }
  // message quand aucun signal n'arrive (appli PC sans visualiseur, rien de joue...)
  bool showMsg = !live && allZero;
  if (showMsg != vzMsgShown) {
    vzMsgShown = showMsg;
    tft.setTextDatum(MC_DATUM);
    if (showMsg) {
      tft.setTextColor(C_DIM, C_BG);
      tft.drawString("En attente du signal audio...", SCR_W / 2, 120, 2);
    } else {
      tft.fillRect(0, 100, SCR_W, 40, C_BG);
    }
  }
}

void drawMusicScreen() {
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
    // En mode plein ecran pochette, pas de mise a jour normale (sauf quand la version HD arrive)
    if (bigDirty) { bigDirty = false; drawFullCoverScreen(); }
    return;
  }
  if (vizMode)   { renderViz(); return; }
  if (lyricFull) { renderLyricFull(); return; }
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

// ----------------------------- Auto-dim + Mode nuit + luminosite auto ------
bool autoDimReset = true;   // true -> reapplique la luminosite au prochain tick
void tickAutoDim() {
  static bool dimmed = false;
  static int  lastPwm = -1;

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
  uint8_t normPwm = pctToPwm(cfg.briPct);
  if (cfg.autoBri) {
    // entre 10 % (obscurite) et le reglage de luminosite (piece eclairee)
    float pct = 10.0f + ((float)cfg.briPct - 10.0f) * powf(ldrLight(), 0.7f);
    normPwm = pctToPwm((int)(pct + 0.5f));
  }
  const bool shouldDim = (millis() - lastTouchMs > (uint32_t)cfg.dimMin * 60000UL);

  uint8_t want = isNight ? nightPwm : (shouldDim ? (uint8_t)min((int)dimPwm, (int)normPwm) : normPwm);
  bool stateChange = autoDimReset || isNight != gNight || shouldDim != dimmed;
  if (stateChange || abs((int)want - lastPwm) >= (cfg.autoBri ? 3 : 1)) {
    blWrite(want);
    lastPwm = want;
  }
  autoDimReset = false;
  gNight = isNight;
  dimmed = shouldDim;
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

// Fin d'un toucher sur l'ecran lecture : sortie des modes plein ecran, balayage, ou tap sur une zone
static void onPlayRelease(int sx, int sy, int ex, int ey) {
  int dx = ex - sx, dy = ey - sy;
  if (fullCover) { fullCover = false; drawMusicScreen(); return; }
  if (vizMode)   { vizMode   = false; drawMusicScreen(); return; }
  if (lyricFull) { lyricFull = false; drawMusicScreen(); return; }
  if (abs(dx) > 40 && abs(dx) > abs(dy)) {                  // balayage : droite = prev, gauche = next
    pressButton(dx > 0 ? 0 : 2);
    return;
  }
  if (abs(dx) > 18 || abs(dy) > 18) return;                 // glissement ambigu : on ignore

  // Tap pochette -> plein ecran
  if (sx >= COVER_X && sx <= COVER_X + COVER_SZ && sy >= COVER_Y && sy <= COVER_Y + COVER_SZ) {
    fullCover = true; drawFullCoverScreen(); return;
  }
  // Tap sur le bloc artiste / album -> visualiseur de spectre
  if (sx >= COL_X && sy >= ARTIST_Y - 6 && sy < BTN_Y - 6) {
    vizMode = true; drawVizScreen(); return;
  }
  // Tap sur la barre de progression -> se deplacer dans le morceau
  if (sy >= BAR_Y + 2 && sy <= BAR_Y + 26 && np.duration > 0) {
    float frac = constrain((float)(ex - BAR_X) / (float)BAR_W, 0.0f, 1.0f);
    float secs = frac * np.duration;
    pendingCmd = "seek:" + String(secs, 1);
    cmdMs = millis();
    basePos = secs; baseMs = millis();                      // retour visuel immediat
    tickBar(true);
    return;
  }
  // Tap sur les paroles -> paroles en grand
  if (sy >= BAR_Y + 34 && (np.lyric.length() || np.lyricPrev.length() || np.lyricNext.length())) {
    lyricFull = true; lyricDirty = false; drawLyricFull(); return;
  }
}

void handleTouch() {
  static bool     down   = false;
  static uint32_t last   = 0;
  static int      startX = -1, startY = -1;   // debut du toucher
  static int      lastX  = -1, lastY  = -1;   // derniere position vue pendant le toucher
  static uint32_t lastIdleTap = 0;  // pour double-tap sur idle

  touchBus();                       // le tactile partage le bus SPI avec la carte SD
  if (!ts.touched()) {
    if (down && cur == SCR_PLAY && startX >= 0) onPlayRelease(startX, startY, lastX, lastY);
    down = false; startX = startY = -1;
    return;
  }

  lastTouchMs = millis();
  {
    TS_Point p0 = ts.getPoint();
    lastX = map(p0.x, 200, 3700, 1, SCR_W);
    lastY = map(p0.y, 240, 3800, 1, SCR_H);
  }

  if (down || millis() - last < 250) return;
  down = true;
  last = millis();

  int x = lastX, y = lastY;

  // Ecran MENU
  if (cur == SCR_MENU) {
    // Bouton + brightness (zone droite)
    if (x > SCR_W-50 && y > 80 && y < 130) {
      cfg.briPct = constrain((int)cfg.briPct + 10, 5, 100);
      saveSettings(); applyBrightness(); autoDimReset = true;
      drawMenuScreen(); return;
    }
    // Bouton - brightness (zone gauche)
    if (x < 50 && y > 80 && y < 130) {
      cfg.briPct = constrain((int)cfg.briPct - 10, 5, 100);
      saveSettings(); applyBrightness(); autoDimReset = true;
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
      wxDetailDay = -1; drawForecastScreen(); return;
    }
    // Tap sur zone previsions (bas) -> detail du jour
    int col = fcHit(x, y);
    if (col >= 0) {
      wxDetailDay = col;
      drawDayDetailScreen(col); return;
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
  if (cur == SCR_IDLE_NEWS || cur == SCR_IDLE_PC) {
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
    // Raccourci : la ligne discrete en bas a gauche ouvre la page "Mon PC"
    if (pcsFresh() && y >= 224 && x < 215) {
      cur = SCR_IDLE_PC; drawIdlePcFull(true); return;
    }
    if (y >= IDLE_NEWS_BAND && y <= IDLE_NEWS_BAND + IDLE_NEWS_H) {
      cur = SCR_IDLE_NEWS; drawNewsScreen(); return;
    }
    if (y >= IDLE_WX_Y && y <= IDLE_WX_Y + IDLE_WX_H) {
      cur = SCR_IDLE_WX; drawForecastScreen(); return;
    }
    return;
  }

  if (cur != SCR_PLAY) return;

  startX = x; startY = y;
  if (fullCover || vizMode || lyricFull) return;           // la sortie se fait au relacher

  // Boutons : reagissent des l'appui
  if (y >= BTN_Y - 6 && y <= BTN_Y + BTN_H + 1) {
    for (int i = 0; i < 3; i++) {
      int bx = COL_X + i * BTN_STEP;
      if (x >= bx - 4 && x <= bx + BTN_W + 4) { startX = -1; pressButton(i); return; }
    }
  }
  // Le reste (pochette, visualiseur, barre, paroles, balayages) est traite dans onPlayRelease()
}

// ----------------------------- Handlers HTTP -------------------------------
void sendReply(bool needCover, bool needIcon, bool needBig = false) {
  JsonDocument r;
  r["ok"] = true;
  r["needCover"] = needCover;
  r["needIcon"] = needIcon;
  r["needBig"] = needBig;
  r["viz"] = wantViz();
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
    np.lyric = ""; np.lyricPrev = ""; np.lyricNext = "";
    sendReply(false, false);
    return;
  }

  String title  = cleanUTF8((const char*)(d["title"]  | "")),
         artist = cleanUTF8((const char*)(d["artist"] | "")),
         album  = cleanUTF8((const char*)(d["album"]  | ""));
  String cid = d["coverId"] | "", app = d["app"] | "", appId = d["appId"] | "";
  bool changed = !hasTrack || title != np.title || artist != np.artist || album != np.album;
  if (changed) {
    np.title = title; np.artist = artist; np.album = album; np.coverId = cid;
    np.lyric = ""; np.lyricPrev = ""; np.lyricNext = ""; lyricDirty = true;
    trackDirty = true;
    coverDirty = true;
    if (fullCover) bigDirty = true;
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
    String ly = d["lyric"] | "", lp = d["lyricPrev"] | "", ln = d["lyricNext"] | "";
    if (ly != np.lyric || lp != np.lyricPrev || ln != np.lyricNext) {
      np.lyric = ly; np.lyricPrev = lp; np.lyricNext = ln; lyricDirty = true;
    }
  }
  np.duration = d["duration"] | 0.0f;
  basePos = d["position"] | 0.0f;
  baseMs = millis();
  hasTrack = true;

  // Pochette : d'abord le cache SD, sinon on la demande au PC
  bool needCover = cid.length() > 0 && cid != coverIdStored;
  if (needCover && sdLoad(SD_DIR_S, cid, coverBuf, COVER_MAX_BYTES, coverLen)) {
    coverIdStored = cid; coverDirty = true; needCover = false;
  }
  // Pochette HD : seulement quand on est en plein ecran
  bool needBig = false;
  if (fullCover && bigBuf && cid.length() > 0 && cid != bigIdStored) {
    if (sdLoad(SD_DIR_B, cid, bigBuf, BIG_MAX_BYTES, bigLen)) { bigIdStored = cid; bigDirty = true; }
    else needBig = true;
  }
  sendReply(needCover, cfg.showApp && appId.length() > 0 && appId != iconIdStored, needBig);
}

void handleCover() {
  if (!decodeJpegBody(coverBuf, COVER_MAX_BYTES, coverLen)) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  coverIdStored = server.arg("id");
  coverDirty = true;
  sdSave(SD_DIR_S, coverIdStored, coverBuf, coverLen);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleBigCover() {
  if (!bigBuf || !decodeJpegBody(bigBuf, BIG_MAX_BYTES, bigLen)) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  bigIdStored = server.arg("id");
  bigDirty = true;
  sdSave(SD_DIR_B, bigIdStored, bigBuf, bigLen);
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
  d["brightness"] = cfg.briPct;
  d["autoBri"] = cfg.autoBri;
  d["ledMode"] = cfg.ledMode;
  d["ledBri"] = cfg.ledBri;
  d["ledNightOff"] = cfg.ledNightOff;
  d["sdCache"] = cfg.sdCache;
  d["sdMax"] = cfg.sdMax;
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
  if (!d["brightness"].isNull()) cfg.briPct = (uint8_t)constrain(d["brightness"].as<int>(), 5, 100);
  if (!d["autoBri"].isNull())    cfg.autoBri    = d["autoBri"].as<bool>();
  if (!d["ledMode"].isNull())    cfg.ledMode    = (uint8_t)constrain(d["ledMode"].as<int>(), 0, 3);
  if (!d["ledBri"].isNull())     cfg.ledBri     = (uint8_t)constrain(d["ledBri"].as<int>(), 5, 100);
  if (!d["ledNightOff"].isNull()) cfg.ledNightOff = d["ledNightOff"].as<bool>();
  if (!d["sdCache"].isNull())    cfg.sdCache    = d["sdCache"].as<bool>();
  if (!d["sdMax"].isNull())      cfg.sdMax      = (uint16_t)constrain(d["sdMax"].as<int>(), 20, 1000);
  bool calOnly = false;
  if (!d["ldrCal"].isNull()) {                       // calibration du capteur de lumiere depuis le panel
    String w = d["ldrCal"].as<String>();
    if (ldrAvg >= 0 && w == "dark")   cfg.ldrDark   = (int16_t)ldrAvg;
    if (ldrAvg >= 0 && w == "bright") cfg.ldrBright = (int16_t)ldrAvg;
    calOnly = true;
  }
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
  if (cfg.sdCache && !sdOk) sdMount();
  if (!calOnly) cur = SCR_NONE;       // force un redessin complet avec les nouveaux reglages
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
  d["maxAlloc"] = ESP.getMaxAllocHeap();
  d["cpuMhz"] = ESP.getCpuFreqMHz();
  d["chipTemp"] = temperatureRead();
  d["screen"] = (cur == SCR_PLAY) ? (fullCover ? "cover" : vizMode ? "viz" : lyricFull ? "lyrics" : "play")
              : (cur == SCR_WAIT) ? "idle" : (cur == SCR_IDLE_WX) ? "weather" : (cur == SCR_IDLE_NEWS) ? "news"
              : (cur == SCR_IDLE_PC) ? "pc" : (cur == SCR_MENU) ? "menu" : "wifi";
  d["title"] = hasTrack ? np.title : "";
  d["artist"] = hasTrack ? np.artist : "";
  d["brightnessPct"] = cfg.briPct;
  d["ldrRaw"] = ldrRaw;
  d["ldrLight"] = (int)(ldrLight() * 100);
  d["ldrDark"] = cfg.ldrDark;
  d["ldrBright"] = cfg.ldrBright;
  d["autoBri"] = cfg.autoBri;
  d["night"] = gNight;
  d["ledMode"] = cfg.ledMode;
  d["viz"] = (vizN > 0 && millis() - vizRxMs < 1500);
  d["vizPackets"] = vizPkts;
  d["sd"]["enabled"] = cfg.sdCache;
  d["sd"]["ok"] = sdOk;
  d["sd"]["sizeMB"] = sdSizeMB;
  d["sd"]["files"] = sdCount;
  d["bigCover"] = (bigLen > 0 && bigIdStored == np.coverId);
  d["pcStats"] = pcsFresh();
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void handleSdClear() {
  sdClear();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handlePcStats() {
  JsonDocument d;
  if (!server.hasArg("plain") || deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  pcs.cpu  = (uint8_t)constrain(d["cpu"]  | 0, 0, 100);
  pcs.ram  = (uint8_t)constrain(d["ram"]  | 0, 0, 100);
  pcs.disk = (uint8_t)constrain(d["disk"] | 0, 0, 100);
  pcs.ramUsed  = d["ramUsed"]  | 0.0f;
  pcs.ramTotal = d["ramTotal"] | 0.0f;
  pcs.diskFree = d["diskFree"] | 0.0f;
  pcs.down = d["down"] | 0;
  pcs.up   = d["up"]   | 0;
  pcs.uptimeMin = d["uptime"] | 0;
  pcs.ms = millis(); pcs.valid = true;
  pcLineDirty = true; pcPageDirty = true;
  server.send(200, "application/json", "{\"ok\":true}");
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
  idleData.description = cleanUTF8((const char*)(d["description"] | ""));
  idleData.windSpeed   = d["windSpeed"]   | 0.0f;
  idleData.location    = cleanUTF8((const char*)(d["location"]    | ""));
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
    idleData.news[idleData.newsCount++] = cleanUTF8(v.as<String>());
  }

  Serial.printf("[idle] temp=%.1f code=%d hasData=%d loc=%s news=%d\n",
    idleData.temp, idleData.weatherCode, (int)idleData.hasData,
    idleData.location.c_str(), idleData.newsCount);

  // Forcer le redessin de l'ecran idle si on est dessus
  if (cur == SCR_WAIT) {
    idleDrawn = false;
  } else if (cur == SCR_IDLE_WX && wxDetailDay < 0) {
    drawForecastScreen();          // la page semaine suit les nouvelles donnees
  } else if (cur == SCR_IDLE_NEWS) {
    drawNewsScreen();
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

// ----------------------------- Page "Mon PC" (stats envoyees par l'appli PC) ----
static String fmtRate(uint32_t kbs) {
  char b[20];
  if (kbs >= 1024) snprintf(b, sizeof(b), "%.1f Mo/s", kbs / 1024.0f);
  else             snprintf(b, sizeof(b), "%u Ko/s", (unsigned)kbs);
  return String(b);
}
static void pcBar(int y, int pct) {
  const int W = SCR_W - 24;
  tft.fillRoundRect(12, y, W, 8, 4, C_TRACK);
  int w = constrain(pct, 0, 100) * W / 100;
  if (w >= 8)     tft.fillRoundRect(12, y, w, 8, 4, pct >= 90 ? RGB565(235, 80, 60) : C_ACCENT);
  else if (w > 0) tft.fillRect(12, y, w, 8, C_ACCENT);
}
static void pcRow(int y, const char* label, const String& value) {
  tft.fillRect(0, y, SCR_W, 17, C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(label, 12, y, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(value, SCR_W - 12, y, 2);
}
void drawIdlePcFull(bool first) {
  if (first) {
    tft.fillScreen(C_BG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString("MON PC", SCR_W / 2, 6, 4);
  }
  if (!pcsFresh()) {
    tft.fillRect(0, 36, SCR_W, 190, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("Pas de donnees du PC", SCR_W / 2, 120, 2);
    return;
  }
  char b[40];
  snprintf(b, sizeof(b), "%u %%", pcs.cpu);
  pcRow(38, "CPU", String(b));                       pcBar(57, pcs.cpu);
  snprintf(b, sizeof(b), "%.1f / %.1f Go", pcs.ramUsed, pcs.ramTotal);
  pcRow(76, "Memoire", String(b));                   pcBar(95, pcs.ram);
  snprintf(b, sizeof(b), "%.0f Go libres", pcs.diskFree);
  pcRow(114, "Disque", String(b));                   pcBar(133, pcs.disk);
  pcRow(154, "Reseau", "v " + fmtRate(pcs.down) + "   ^ " + fmtRate(pcs.up));
  uint32_t m = pcs.uptimeMin;
  if (m >= 1440) snprintf(b, sizeof(b), "%u j %u h", (unsigned)(m / 1440), (unsigned)((m % 1440) / 60));
  else           snprintf(b, sizeof(b), "%u h %02u", (unsigned)(m / 60), (unsigned)(m % 60));
  pcRow(180, "Allume depuis", String(b));
}

// ============================================================================
//  Pages detail v1.7 : prevision semaine + actualites
//  Dessin "one-shot" a l'entree de la page (jamais dans loop) : un seul
//  tft.fillScreen() au changement de page, puis rien ne bouge.
//  Aucune String : uniquement des char[] sur la pile.
// ============================================================================

// Tronque 'in' pour tenir dans maxW pixels avec la police COURANTE de tft ; ajoute "..."
static void ellipsizeOn(TFT_eSPI& g, const char* in, char* out, size_t cap, int maxW) {
  strlcpy(out, in, cap - 3);                       // garde la place des 3 points
  if (g.textWidth(out) <= maxW) return;
  const int dotsW = g.textWidth("...");
  size_t n = strlen(out);
  while (n > 0 && g.textWidth(out) + dotsW > maxW) out[--n] = 0;
  while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
  { size_t m = strlen(out); snprintf(out + m, cap - m, "..."); }
}

static inline void ellipsize(const char* in, char* out, size_t cap, int maxW) { ellipsizeOn(tft, in, out, cap, maxW); }

// Ecrit "val" + petit rond de degre. align : 0 gauche, 1 centre, 2 droite. Renvoie le x de fin.
static int drawTemp(int x, int cy, int val, uint16_t fg, uint16_t bg,
                    int ringR, int ringTh, int ringDy, uint8_t align) {
  char b[8]; snprintf(b, sizeof(b), "%d", val);
  const int tw = tft.textWidth(b), total = tw + ringR * 2 + 3;
  const int x0 = (align == 1) ? x - total / 2 : (align == 2) ? x - total : x;
  tft.setTextColor(fg, bg);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(b, x0, cy);
  ui::degreeRing(tft, x0 + tw + 3 + ringR, cy + ringDy, ringR, fg, bg, ringTh);
  return x0 + total;
}

// ---- Grille des previsions : 5 colonnes de 58 px ---------------------------
static const int FC_N = 5, FC_W = 58, FC_GAP = 4, FC_Y = 118, FC_H = 110;
static inline int fcCount()        { return idleData.forecastDays < FC_N ? idleData.forecastDays : FC_N; }
static inline int fcX0(int n)      { return (SCR_W - (n * FC_W + (n - 1) * FC_GAP)) / 2; }

int fcHit(int x, int y) {
  const int n = fcCount();
  if (n <= 0 || y < FC_Y || y > FC_Y + FC_H) return -1;
  const int rel = x - fcX0(n);
  if (rel < 0) return -1;
  const int i = rel / (FC_W + FC_GAP);
  return i < n ? i : -1;
}

void drawForecastScreen() {
  static const char* const DAY3[7] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
  tft.fillScreen(C_BG);                                    // changement de page uniquement

  // ---- Carte "maintenant" (haut) ----
  tft.fillRoundRect(10, 6, 300, 106, ui::R_CARD, ui::C_CARD);
  tft.fillSmoothCircle(24, 22, 3, ui::C_AMBER, ui::C_CARD);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(ui::C_MUTED, ui::C_CARD);
  char buf[64];
  ellipsize(idleData.location.length() ? idleData.location.c_str() : "Meteo", buf, sizeof(buf), 140);
  tft.drawString(buf, 33, 22);

  if (!idleData.hasData) {
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Meteo indisponible", SCR_W / 2, 66);
  } else {
    // Grosse temperature a gauche
    tft.setFreeFont(&FreeSansBold24pt7b);
    drawTemp(22, 57, (int)lroundf(idleData.temp), C_TEXT, ui::C_CARD, 5, 2, -13, 0);
    // Description sous la temperature
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(ui::C_MUTED, ui::C_CARD);
    tft.setTextDatum(ML_DATUM);
    ellipsize(idleData.description.c_str(), buf, sizeof(buf), 150);
    tft.drawString(buf, 22, 92);
    // Icone + 3 lignes compactes a droite
    ui::drawWxIcon(tft, 240, 30, ui::wmoToIcon(idleData.weatherCode), 0.8f, ui::C_CARD);
    tft.setTextColor(ui::C_MUTED, ui::C_CARD);
    tft.drawString("Ressenti", 184, 66);
    tft.drawString("Vent",     184, 83);
    tft.drawString("Humid.",   184, 100);
    tft.setTextColor(C_TEXT, ui::C_CARD);
    tft.setTextDatum(MR_DATUM);
    drawTemp(298, 66, (int)lroundf(idleData.feelsLike), C_TEXT, ui::C_CARD, 3, 1, -5, 2);
    snprintf(buf, sizeof(buf), "%d km/h", (int)lroundf(idleData.windSpeed));
    tft.setTextColor(C_TEXT, ui::C_CARD); tft.setTextDatum(MR_DATUM);
    tft.drawString(buf, 298, 83);
    snprintf(buf, sizeof(buf), "%d %%", (int)lroundf(idleData.humidity));
    tft.drawString(buf, 298, 100);
  }

  // ---- Grille 5 jours (bas) ----
  const int n = fcCount();
  if (n > 0) {
    struct tm tnow; getLocalTime(&tnow, 0);
    const int x0 = fcX0(n);
    for (int i = 0; i < n; i++) {
      const int x = x0 + i * (FC_W + FC_GAP), cx = x + FC_W / 2;
      const uint16_t bg = (i == 0) ? ui::C_CARD2 : ui::C_CARD;
      tft.fillRoundRect(x, FC_Y, FC_W, FC_H, 10, bg);
      // Nom du jour
      tft.setFreeFont(&FreeSansBold9pt7b);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(i == 0 ? ui::C_AMBER : ui::C_MUTED, bg);
      tft.drawString(i == 0 ? "AUJ." : DAY3[(tnow.tm_wday + i) % 7], cx, FC_Y + 15);
      // Mini icone
      ui::drawWxIcon(tft, cx, FC_Y + 48, ui::wmoToIcon(idleData.forecast[i].code), 0.72f, bg);
      // Max (blanc, gras) puis min (gris)
      tft.setFreeFont(&FreeSansBold12pt7b);
      drawTemp(cx, FC_Y + 79, (int)lroundf(idleData.forecast[i].tMax), C_TEXT, bg, 3, 1, -8, 1);
      tft.setFreeFont(&FreeSans9pt7b);
      drawTemp(cx, FC_Y + 98, (int)lroundf(idleData.forecast[i].tMin), ui::C_MUTED, bg, 2, 1, -5, 1);
    }
  } else {
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(ui::C_MUTED, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Previsions en attente...", SCR_W / 2, 170);
  }
  tft.setTextFont(1);                                      // revient a la police par defaut
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TRACK, C_BG);
  tft.drawString("Tap: detail    2x tap: retour", SCR_W / 2, 231);
}

// ----------------------------- Page actualites -----------------------------
void drawNewsScreen() {
  tft.fillScreen(C_BG);                                    // remise a zero complete : plus de residus de l'accueil

  // Titre
  tft.fillRoundRect(10, 10, 3, 18, 1, ui::C_AMBER);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Actualites", 20, 19);

  tft.setFreeFont(&FreeSans9pt7b);
  if (idleData.newsCount == 0) {
    tft.setTextColor(ui::C_MUTED, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Aucune actualite disponible", SCR_W / 2, 120);
  } else {
    const int ROW_H = 34, GAP = 4, Y0 = 38, MAXR = 5, TXT_X = 36, TXT_W = 300 - (TXT_X - 10) - 12;
    const int n = idleData.newsCount < MAXR ? idleData.newsCount : MAXR;
    char line[112];
    for (int i = 0; i < n; i++) {
      const int y = Y0 + i * (ROW_H + GAP);
      tft.fillRoundRect(10, y, 300, ROW_H, 8, ui::C_CARD);
      tft.fillRoundRect(18, y + 9, 3, ROW_H - 18, 1, ui::C_AMBER);
      ellipsize(idleData.news[i].c_str(), line, sizeof(line), TXT_W);
      tft.setTextColor(C_TEXT, ui::C_CARD);
      tft.setTextDatum(ML_DATUM);
      tft.drawString(line, TXT_X, y + ROW_H / 2);
    }
  }
  tft.setTextFont(1);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TRACK, C_BG);
  tft.drawString("Tap pour revenir", SCR_W / 2, 231);
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
  int barFill = map(cfg.briPct, 0, 100, 0, 170);
  tft.fillRect(75, 93, 170, 14, RGB565(25,25,40));
  tft.fillRect(75, 93, barFill, 14, C_ACCENT);
  char bpct[8]; snprintf(bpct, sizeof(bpct), "%d%%", cfg.briPct);
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

// ----------------------------- Detail d'un jour meteo (v1.7) -----------
static uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t /*0..255*/) {
  const int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  const int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar * (255 - t) + br * t) / 255) << 11) |
                    (((ag * (255 - t) + bg * t) / 255) << 5)  |
                     ((ab * (255 - t) + bb * t) / 255));
}

// "Max 14°" : libelle gris + valeur coloree, centre sur cxc
static void drawLabelTemp(int cxc, int cy, const char* label, int val, uint16_t col) {
  tft.setFreeFont(&FreeSans9pt7b);
  const int lw = tft.textWidth(label);
  tft.setFreeFont(&FreeSansBold12pt7b);
  char b[8]; snprintf(b, sizeof(b), "%d", val);
  const int total = lw + 8 + tft.textWidth(b) + 9;
  const int x0 = cxc - total / 2;
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_DIM, ui::C_CARD); tft.setTextDatum(ML_DATUM);
  tft.drawString(label, x0, cy + 2);
  tft.setFreeFont(&FreeSansBold12pt7b);
  drawTemp(x0 + lw + 8, cy, val, col, ui::C_CARD, 3, 1, -8, 0);
}

void drawDayDetailScreen(int day) {
  if (day < 0 || day >= idleData.forecastDays) day = 0;
  const DayForecast& f = idleData.forecast[day];
  const uint16_t C_COLD = RGB565(100, 210, 255);              // bleu clair
  tft.fillScreen(C_BG);                                       // changement de page uniquement

  // Date du jour affiche (aujourd'hui + day)
  time_t t = time(nullptr) + (time_t)day * 86400;
  struct tm tm; localtime_r(&t, &tm);
  char buf[48];

  // Carte centrale 280x200
  const int CX = 20, CY = 6, CW = 280, CH = 200, MID = CX + CW / 2;
  tft.fillRoundRect(CX, CY, CW, CH, 10, ui::C_CARD);

  // En-tete : nom du jour + date
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextColor(C_TEXT, ui::C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(dayNames[tm.tm_wday], MID, CY + 26);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_DIM, ui::C_CARD);
  snprintf(buf, sizeof(buf), day == 0 ? "Aujourd'hui - %d %s" : "%d %s", tm.tm_mday, monthNames[tm.tm_mon]);
  tft.drawString(buf, MID, CY + 52);

  // Grande icone + description
  ui::drawWxIcon(tft, MID, CY + 96, ui::wmoToIcon(f.code), 1.3f, ui::C_CARD);
  tft.setTextColor(C_DIM, ui::C_CARD);
  tft.drawString(wmoDesc(f.code).c_str(), MID, CY + 138);

  // Min / Max
  drawLabelTemp(CX + CW / 4,     CY + 162, "Max", (int)lroundf(f.tMax), ui::C_AMBER);
  drawLabelTemp(CX + CW * 3 / 4, CY + 162, "Min", (int)lroundf(f.tMin), C_COLD);

  // Barre de plage thermique (echelle = min/max de toute la prevision)
  float gMin = f.tMin, gMax = f.tMax;
  for (int i = 0; i < idleData.forecastDays; i++) {
    if (idleData.forecast[i].tMin < gMin) gMin = idleData.forecast[i].tMin;
    if (idleData.forecast[i].tMax > gMax) gMax = idleData.forecast[i].tMax;
  }
  const float span = (gMax - gMin) < 1.0f ? 1.0f : (gMax - gMin);
  const int BX = CX + 24, BW = CW - 48, BY = CY + 186;
  tft.fillRoundRect(BX, BY, BW, 4, 2, C_TRACK);
  int x1 = BX + (int)((f.tMin - gMin) / span * BW), x2 = BX + (int)((f.tMax - gMin) / span * BW);
  if (x2 - x1 < 8) x2 = x1 + 8;
  if (x2 > BX + BW) { x2 = BX + BW; x1 = x2 - 8; }
  for (int px = x1; px <= x2; px++)
    tft.drawFastVLine(px, BY, 4, lerp565(C_COLD, ui::C_AMBER, (uint8_t)(255L * (px - x1) / (x2 - x1))));
  tft.fillSmoothCircle(x1, BY + 2, 3, C_COLD, ui::C_CARD);
  tft.fillSmoothCircle(x2, BY + 2, 3, ui::C_AMBER, ui::C_CARD);

  // Bouton retour discret
  tft.fillRoundRect(110, 212, 100, 24, 10, C_BTN);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT, C_BTN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("< Retour", 160, 224);
  tft.setTextFont(1);
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
    if (!vizUdpOn) { vizUdp.begin(VIZ_PORT); vizUdpOn = true; }
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
  pinMode(LDR_PIN, INPUT);
  ledInit();
  ledPwm(0, 0, 0);
  if (cfg.sdCache) { sdMount(); touchBus(); }   // carte SD (le bus est partage avec le tactile)

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tftOutput);

  coverBuf = (uint8_t*)malloc(COVER_MAX_BYTES);
  iconBuf  = (uint8_t*)malloc(ICON_MAX_BYTES);
  bigBuf   = (uint8_t*)malloc(BIG_MAX_BYTES);    // pochette HD (peut rester nul si la memoire manque)
  titleSpr.createSprite(COL_W, TITLE_H);
  barSpr.createSprite(BAR_W, BAR_H);
  eqSpr.createSprite(100, 72);
  if (!initModernDashboard()) Serial.println("[ui] RAM insuffisante pour les sprites du dashboard");
  Serial.printf("[ui] heap libre apres sprites: %u (max bloc %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/nowplaying", HTTP_POST, handleNowPlaying);
  server.on("/api/cover", HTTP_POST, handleCover);
  server.on("/api/icon", HTTP_POST, handleIcon);
  server.on("/api/coverbig", HTTP_POST, handleBigCover);
  server.on("/api/pcstats", HTTP_POST, handlePcStats);
  server.on("/api/sdclear", HTTP_POST, handleSdClear);
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
  pollViz();
  tickLdr();
  tickLed();

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
  bool onDetailScreen = (cur == SCR_IDLE_WX || cur == SCR_IDLE_NEWS || cur == SCR_MENU || cur == SCR_IDLE_PC);
  if (!onDetailScreen && want != cur) {
    cur = want;
    lastClockMin = -1;   // force le redessin de l'heure au changement d'ecran
    fullCover = false;   // quitter le mode fullCover si on change d'ecran
    vizMode = false; lyricFull = false;
    idleDrawn = false;   // forcer redessin de l'ecran idle
    if (cur == SCR_PLAY) drawMusicScreen();
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
    if (cur == SCR_WAIT) animateIdle();   // sinon les sprites de l'accueil se repoussent sur la page actus/meteo
  } else if (cur == SCR_IDLE_WX || cur == SCR_IDLE_NEWS || cur == SCR_MENU || cur == SCR_IDLE_PC) {
    handleTouch();   // tap = retour idle
    if (cur == SCR_IDLE_PC) {            // page "Mon PC" : redessin a chaque nouvelle mesure (ou quand elle devient perimee)
      static bool wasFresh = true;
      bool f = pcsFresh();
      if (pcPageDirty || f != wasFresh) { pcPageDirty = false; wasFresh = f; drawIdlePcFull(false); }
    }
  } else {
    animateWait();
  }
}
