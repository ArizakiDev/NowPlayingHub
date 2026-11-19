
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
#include "esp_system.h"
#if !defined(NPH_RELEASE) && defined(__has_include)
#  if __has_include("config.h")
#    include "config.h"
#  endif
#endif
#include "settings.h"
#include "TextLayout.h"
#include "SysDiag.h"
#include <SPIFFS.h>
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef OTA_TOKEN
#define OTA_TOKEN "nowplaying"
#endif
#include <HTTPClient.h>
void drawForecastScreen();
void wxTapTab(int x);
static void ellipsizeOn(TFT_eSPI& g, const char* in, char* out, size_t cap, int maxW);
int  fcHit(int x, int y);
void drawNewsScreen();
void buildNewsTicker();
void drawMenuScreen();
void menuTouchDown(int x, int y);
void menuTouchDrag(int x, int y);
void menuTouchUp(int x);
void tickMenuScreen();
void drawDayDetailScreen(int day);
void drawNewsDetailScreen(int newsIndex);
void drawSystemMemoryScreen();
void drawLyricFull();
void tickLyricBar(bool force = false);

static inline bool timeNow(struct tm* t) {
  const time_t n = time(nullptr);
  localtime_r(&n, t);
  return t->tm_year > (2016 - 1900);
}

#define RGB565(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
static uint16_t cAccent = RGB565(250, 45, 72);
#define C_ACCENT cAccent
static const uint16_t C_BG    = 0x0000;
static const uint16_t C_TEXT  = 0xFFFF;
static const uint16_t C_DIM   = RGB565(142, 142, 147);
static const uint16_t C_TRACK = RGB565(58, 58, 60);
static const uint16_t C_PLACE = RGB565(28, 28, 30);
static const uint16_t C_BTN   = RGB565(44, 44, 46);

static const int SCR_W = 320, SCR_H = 240;
static const int COVER_X = 10, COVER_Y = 34, COVER_SZ = 120;
static const int COL_X = 142, COL_W = 168;
static const int TITLE_Y = 40, TITLE_H = 30;
static const int ARTIST_Y = 80, ALBUM_Y = 102;
static const int BTN_Y = 130, BTN_H = 36, BTN_W = 52, BTN_STEP = 58;
static const int BTN_XS[3] = { COL_X, COL_X + 54, COL_X + 124 };
static const int BTN_WS[3] = { 44, 60, 44 };
static const int BAR_X = 10, BAR_Y = 166, BAR_W = 300, BAR_H = 40;

struct Settings {
  uint8_t  briPct     = 100;
  uint32_t accent     = 0xFA2D48;
  uint16_t scroll     = 45;
  bool     showApp    = true;
  bool     nightOn    = true;
  uint8_t  nightStart = 22;
  uint8_t  nightEnd   = 7;
  uint8_t  nightBri   = 8;
  uint8_t  dimMin     = 3;
  uint8_t  dimBri     = 15;
  bool     autoBri    = false;
  int16_t  ldrDark    = 2600;
  int16_t  ldrBright  = 300;
  uint8_t  ledMode    = 0;
  uint8_t  ledBri     = 40;
  bool     ledNightOff = true;
  bool     sdCache    = false;
  uint16_t sdMax      = 150;
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
uint32_t dynAccent = 0;
void applyAccent() {
  uint32_t a = dynAccent ? dynAccent : cfg.accent;
  cAccent = RGB565((a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF);
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  static void blInit()          { ledcAttach(TFT_BL, 5000, 8); }
  static void blWrite(uint8_t v){ ledcWrite(TFT_BL, v); }
#else
  static void blInit()          { ledcSetup(7, 5000, 8); ledcAttachPin(TFT_BL, 7); }
  static void blWrite(uint8_t v){ ledcWrite(7, v); }
#endif
static uint8_t pctToPwm(int pct) { return (uint8_t)constrain(pct * 255 / 100, 13, 255); }
void applyBrightness();

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
static float ldrLight() {
  if (ldrAvg < 0) return 1.0f;
  float d = (float)cfg.ldrDark, b = (float)cfg.ldrBright;
  if (fabsf(b - d) < 50.0f) return 1.0f;
  return constrain((ldrAvg - d) / (b - d), 0.0f, 1.0f);
}

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

TFT_eSPI    tft;
TFT_eSprite titleSpr(&tft);
TFT_eSprite barSpr(&tft);
#include "Utf8Ascii.h"
#include "ModernDashboard.h"
WebServer   server(80);
SPIClass    touchSPI(VSPI);
XPT2046_Touchscreen ts(33, 36);

struct Track {
  String title, artist, album, state = "stopped", coverId, app, appId;
  float  duration = 0;
  String lyric, lyricPrev, lyricNext;
} np;
bool lyricDirty = false;
static uint32_t lyricSwapAt = 0;
float    basePos = 0;
uint32_t baseMs = 0, lastRx = 0;
bool     hasTrack = false;

uint8_t *coverBuf = nullptr, *iconBuf = nullptr;
size_t   coverLen = 0, iconLen = 0;
String   coverIdStored, iconIdStored;

static String   coverFailId;
static uint8_t  coverFails = 0;
static uint32_t coverFailMs = 0;
static const uint8_t COVER_MAX_FAILS = 3;

String   pendingCmd;
uint32_t cmdMs = 0;
int      flashIdx = -1;
static int      pcVol = -1;
static bool     pcMute = false;
static uint32_t volToastUntil = 0;
static bool     volToastDirty = false;
uint32_t flashUntil = 0;

enum Screen { SCR_NONE, SCR_WIFI, SCR_WAIT, SCR_PLAY, SCR_IDLE_WX, SCR_IDLE_NEWS, SCR_MENU, SCR_SYS };
Screen cur = SCR_NONE;

bool trackDirty = false, coverDirty = false, stateDirty = false, headerDirty = false;
static String wifiError = "";

#define PORTAL_SSID "NowPlaying-Setup"
String   wifiSsid, wifiPass;
bool     portalActive  = false;
bool     everConnected = false;
String   portalNets;
DNSServer dnsServer;

static bool wifiFastLoad(uint8_t* bssid, int* ch) {
  prefs.begin("wifi", true);
  bool ok = prefs.getString("fcSsid", "") == wifiSsid && prefs.getUChar("fcCh", 0) > 0 &&
            prefs.getBytes("fcBssid", bssid, 6) == 6;
  if (ok) *ch = prefs.getUChar("fcCh", 0);
  prefs.end();
  return ok;
}
static void wifiFastSave(int ch, const uint8_t* bssid) {
  uint8_t old[6] = {0};
  prefs.begin("wifi", false);
  bool same = prefs.getString("fcSsid", "") == wifiSsid && prefs.getUChar("fcCh", 0) == ch &&
              prefs.getBytes("fcBssid", old, 6) == 6 && memcmp(old, bssid, 6) == 0;
  if (!same) {
    prefs.putString("fcSsid", wifiSsid); prefs.putUChar("fcCh", (uint8_t)ch); prefs.putBytes("fcBssid", bssid, 6);
  }
  prefs.end();
}

String otaToken;
void loadWifi() {
  prefs.begin("wifi", true);
  wifiSsid = prefs.getString("ssid", WIFI_SSID);
  wifiPass = prefs.getString("pass", WIFI_PASS);
  otaToken = prefs.getString("otk", OTA_TOKEN);
  prefs.end();
}
static bool tokenOk(const String& t) {
  if (t.length() != otaToken.length()) return false;
  uint8_t d = 0;
  for (size_t i = 0; i < t.length(); i++) d |= (uint8_t)(t[i] ^ otaToken[i]);
  return d == 0;
}
static String lastShownWifiErr = "\x01";
int      titleW = 0;
uint32_t scrollStart = 0;
int      lastOff = -9999;
bool     titleStaticDrawn = false;
int      lastSec = -1;

uint32_t lastTouchMs = 0;
static int      wxDetailDay   = -1;
static uint32_t lastDetailTap = 0;
static int      newsDetailIdx = -1;
static String   newsDetailTitle;
int      lastClockMin = -1;

bool fullCover = false;

bool     vizMode   = false;
bool     lyricFull = false;
static bool     lyricSeen = false;
static uint8_t  lyricPage = 0;
static uint32_t lyricPageMs = 0;
static uint32_t lyricHintUntil = 0;
bool     bigDirty  = false;
uint8_t* bigBuf    = nullptr;
size_t   bigLen    = 0;
String   bigIdStored;

static void bigRelease() { if (bigBuf) { free(bigBuf); bigBuf = nullptr; } bigLen = 0; bigIdStored = ""; }
static bool bigEnsure() {
  if (bigBuf) return true;
  if (ESP.getMaxAllocHeap() < (uint32_t)BIG_MAX_BYTES + 10000UL) return false;
  bigBuf = (uint8_t*)malloc(BIG_MAX_BYTES);
  return bigBuf != nullptr;
}
static bool gNight = false;
extern bool autoDimReset;
void applyBrightness() {
  autoDimReset = true;
  if (!gNight) blWrite(pctToPwm(cfg.briPct));
}

struct LoopStat {
  uint32_t winStart = 0, n = 0, sumUs = 0, maxUs = 0, slow = 0;
  uint32_t avgUs = 0, worstUs = 0, hz = 0, slowLast = 0;
  uint32_t slowTotal = 0;
};
static LoopStat loopStat;
static void loopStatRecord(uint32_t us) {
  loopStat.n++; loopStat.sumUs += us;
  if (us > loopStat.maxUs) loopStat.maxUs = us;
  if (us >= 100000UL) { loopStat.slow++; loopStat.slowTotal++; }
  const uint32_t now = millis(), win = now - loopStat.winStart;
  if (win >= 5000) {
    loopStat.avgUs = loopStat.n ? loopStat.sumUs / loopStat.n : 0;
    loopStat.worstUs = loopStat.maxUs;
    loopStat.hz = (uint32_t)((uint64_t)loopStat.n * 1000ULL / win);
    loopStat.slowLast = loopStat.slow;
    loopStat.winStart = now; loopStat.n = 0; loopStat.sumUs = 0; loopStat.maxUs = 0; loopStat.slow = 0;
  }
}

static const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Mise sous tension";
    case ESP_RST_EXT:       return "Reset externe";
    case ESP_RST_SW:        return "Redemarrage logiciel";
    case ESP_RST_PANIC:     return "Plantage (panic)";
    case ESP_RST_INT_WDT:   return "Watchdog (interruption)";
    case ESP_RST_TASK_WDT:  return "Watchdog (tache)";
    case ESP_RST_WDT:       return "Watchdog";
    case ESP_RST_DEEPSLEEP: return "Reveil de veille profonde";
    case ESP_RST_BROWNOUT:  return "Chute de tension";
    default:                return "Inconnue";
  }
}

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

uint32_t pausedSince  = 0;
bool     forcedIdle   = false;

struct DayForecast { float tMax = 0, tMin = 0; int code = 0; };
struct HourFc { int8_t t = 0; uint8_t pop = 0, code = 0, hour = 0; };
struct NewsMeta { String desc, when, src; };
static const size_t NEWS_DESC_MAX = 320;
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
  NewsMeta newsMeta[10];
  bool   hasData     = false;
  DayForecast forecast[7];
  int    forecastDays = 0;
  HourFc hourly[12];
  int    hourlyN = 0;
  float  pressure = 0;
} idleData;

struct IdleNetConfig {
  float  lat    = 50.8504f;
  float  lon    = 4.3517f;
  String loc    = "Bruxelles";
  String rssUrl = "https://news.google.com/rss?hl=fr&gl=FR&ceid=FR:fr";
  String tz     = "CET-1CEST,M3.5.0,M10.5.0/3";
} idleCfg;

void loadIdleConfig() {
  prefs.begin("idle", true);
  idleCfg.lat     = prefs.getFloat("ilat",  50.8504f);
  idleCfg.lon     = prefs.getFloat("ilon",  4.3517f);
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

static uint32_t lastWxFetch  = 0;
static uint32_t lastRssFetch = 0;
static bool     firstFetchDone = false;

static const int IDLE_NEWS_BAND = 0;
static const int IDLE_NEWS_H   = 36;
static const int IDLE_TOPBAR_Y = 22;
static const int IDLE_CLK_Y    = 30;
static const int IDLE_CLK_FONT = 7;
static const int IDLE_DATE_Y   = 104;
static const int IDLE_SEP_Y    = 122;
static const int IDLE_WX_Y     = 42;
static const int IDLE_WX_H     = 188;
static const int IDLE_NEWS_Y   = IDLE_NEWS_BAND;
static const int IDLE_IP_Y     = 236;
static bool     idleDrawn     = false;
static int      idleClockMin  = -1;
static int      idleClockSec  = -1;
static uint32_t lastWifiDraw  = 0;
static uint32_t lastSecsDraw  = 0;
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

struct WxRes {
  float temp, feels, wind, hum, uv; int code;
  char  sr[8], ss[8];
  int   days; float tMax[7], tMin[7]; int dCode[7];
  int   hn; int8_t hT[12]; uint8_t hP[12], hC[12], hH[12]; float press;
};
static WxRes wxRes;
static volatile bool wxReady = false, wxBusy = false, wxFailed = false;
static float wxLat = 0, wxLon = 0;

static void wxTask(void*) {
  {
    char url[480];
    snprintf(url, sizeof(url),
      "http://api.open-meteo.com/v1/forecast"
      "?latitude=%.4f&longitude=%.4f"
      "&current=temperature_2m,apparent_temperature,weather_code"
      ",wind_speed_10m,relative_humidity_2m,uv_index,pressure_msl"
      "&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min,weather_code"
      "&hourly=temperature_2m,precipitation_probability,weather_code&forecast_hours=12"
      "&timezone=auto&forecast_days=7", wxLat, wxLon);
    HTTPClient http;
    http.begin(url);
    http.setConnectTimeout(3000);
    http.setTimeout(4000);
    const int code = http.GET();
    if (code == 200) {
      String body = http.getString();
      JsonDocument doc;
      if (!deserializeJson(doc, body)) {
        WxRes w; memset(&w, 0, sizeof(w));
        JsonObject c = doc["current"].as<JsonObject>();
        w.temp  = c["temperature_2m"]       | 0.0f;
        w.feels = c["apparent_temperature"] | 0.0f;
        w.code  = c["weather_code"]         | 0;
        w.wind  = c["wind_speed_10m"]       | 0.0f;
        w.hum   = c["relative_humidity_2m"] | 0.0f;
        w.uv    = c["uv_index"]             | -1.0f;
        const char* sr = doc["daily"]["sunrise"][0] | "";
        const char* ss = doc["daily"]["sunset"][0]  | "";
        if (strlen(sr) >= 16) { memcpy(w.sr, sr + 11, 5); w.sr[5] = 0; }
        if (strlen(ss) >= 16) { memcpy(w.ss, ss + 11, 5); w.ss[5] = 0; }
        JsonArray tMax = doc["daily"]["temperature_2m_max"];
        JsonArray tMin = doc["daily"]["temperature_2m_min"];
        JsonArray wc   = doc["daily"]["weather_code"];
        for (int i = 0; i < 7 && i < (int)tMax.size(); i++) {
          w.tMax[i] = tMax[i] | 0.0f; w.tMin[i] = tMin[i] | 0.0f; w.dCode[i] = wc[i] | 0; w.days++;
        }
        w.press = c["pressure_msl"] | 0.0f;
        JsonArray hT = doc["hourly"]["temperature_2m"];
        JsonArray hP = doc["hourly"]["precipitation_probability"];
        JsonArray hC = doc["hourly"]["weather_code"];
        JsonArray hS = doc["hourly"]["time"];
        for (int i = 0; i < 12 && i < (int)hT.size(); i++) {
          w.hT[i] = (int8_t)lroundf(hT[i] | 0.0f);
          w.hP[i] = (uint8_t)constrain((int)(hP[i] | 0), 0, 100);
          w.hC[i] = (uint8_t)(hC[i] | 0);
          const char* tt = hS[i] | "";
          w.hH[i] = strlen(tt) >= 13 ? (uint8_t)((tt[11] - '0') * 10 + (tt[12] - '0')) : 0;
          w.hn++;
        }
        wxRes = w;
        wxReady = true;
      } else {
        wxFailed = true;
      }
    } else {
      Serial.printf("[wx-esp] HTTP %d\n", code);
      wxFailed = true;
    }
    http.end();
  }
  wxBusy = false;
  vTaskDelete(NULL);
}

void fetchWeatherFromNet() {
  if (wxBusy || WiFi.status() != WL_CONNECTED) return;
  wxBusy = true; wxLat = idleCfg.lat; wxLon = idleCfg.lon;
  if (xTaskCreatePinnedToCore(wxTask, "wx", 8192, nullptr, 1, nullptr, 0) != pdPASS) wxBusy = false;
}

static void applyWx() {
  if (!wxReady) return;
  wxReady = false;
  idleData.temp = wxRes.temp; idleData.feelsLike = wxRes.feels; idleData.weatherCode = wxRes.code;
  idleData.windSpeed = wxRes.wind; idleData.humidity = wxRes.hum; idleData.uvIndex = wxRes.uv;
  idleData.description = wmoDesc(wxRes.code);
  idleData.location    = idleCfg.loc;
  idleData.sunrise = wxRes.sr; idleData.sunset = wxRes.ss;
  idleData.forecastDays = 0;
  for (int i = 0; i < wxRes.days && i < 7; i++) {
    idleData.forecast[i].tMax = wxRes.tMax[i]; idleData.forecast[i].tMin = wxRes.tMin[i]; idleData.forecast[i].code = wxRes.dCode[i];
    idleData.forecastDays++;
  }
  idleData.pressure = wxRes.press;
  idleData.hourlyN = 0;
  for (int i = 0; i < wxRes.hn && i < 12; i++) {
    idleData.hourly[i].t = wxRes.hT[i]; idleData.hourly[i].pop = wxRes.hP[i];
    idleData.hourly[i].code = wxRes.hC[i]; idleData.hourly[i].hour = wxRes.hH[i];
    idleData.hourlyN++;
  }
  idleData.hasData = true;
  Serial.printf("[wx-esp] %.1fC code=%d\n", idleData.temp, idleData.weatherCode);
}

void tickNetFetch() {
  applyWx();
  if (WiFi.status() != WL_CONNECTED) return;
  const uint32_t now = millis();
  if (!firstFetchDone) { firstFetchDone = true; fetchWeatherFromNet(); lastWxFetch = now; return; }
  if (wxFailed && !wxBusy) { wxFailed = false; lastWxFetch = now - 900000UL + 60000UL; }
  if (now - lastWxFetch >= 900000UL) { lastWxFetch = now; fetchWeatherFromNet(); }
}

static int   newsTickerOffset = 0;

static String newsFullStr = "";

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

static bool ntpConfigured = false;
static bool ntpSynced     = false;

static void ensureNtp() {
  if (!ntpSynced) {
    struct tm t;
    if (timeNow(&t)) ntpSynced = true;
  }
}

static String clockStr() {
  struct tm t;
  if (!timeNow(&t)) return "--:--";
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return buf;
}
static String secsStr() {
  struct tm t;
  if (!timeNow(&t)) return ":--";
  char buf[4];
  snprintf(buf, sizeof(buf), ":%02d", t.tm_sec);
  return buf;
}

String fitText(String s, int font, int maxW) {
  if (tft.textWidth(s, font) <= maxW) return s;
  while (s.length() > 1 && tft.textWidth(s + "...", font) > maxW) s.remove(s.length() - 1);
  return s + "...";
}

static int wrapText(const String& s, int font, int maxW, int maxLines, String* out) {
  String rest = s;
  rest.trim();
  int n = 0;
  while (rest.length() && n < maxLines) {
    if (tft.textWidth(rest, font) <= maxW) { out[n++] = rest; break; }
    if (n == maxLines - 1) { out[n++] = fitText(rest, font, maxW); break; }
    int cut = -1;
    for (int i = 0; i < (int)rest.length(); i++) {
      if (rest[i] != ' ') continue;
      if (tft.textWidth(rest.substring(0, i), font) > maxW) break;
      cut = i;
    }
    if (cut <= 0) {
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

bool decodeJpegBody(uint8_t* buf, size_t cap, size_t& outLen, const char* tag = "img") {
  outLen = 0;
  if (!buf) { Serial.printf("[%s] buffer non alloue\n", tag); return false; }
  const String& b64 = server.arg("plain");
  const size_t n64 = b64.length();
  if (n64 < 8) { Serial.printf("[%s] corps vide/tronque (len=%u) : RAM ou timeout reseau ?\n", tag, (unsigned)n64); return false; }
  const size_t est = (n64 / 4) * 3;
  if (est > cap + 3) { Serial.printf("[%s] trop gros : ~%u o > %u o\n", tag, (unsigned)est, (unsigned)cap); return false; }
  size_t o = 0;
  int rc = mbedtls_base64_decode(buf, cap, &o, (const uint8_t*)b64.c_str(), n64);
  if (rc != 0 || o < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {
    Serial.printf("[%s] base64/SOI invalide rc=%d o=%u\n", tag, rc, (unsigned)o);
    return false;
  }
  bool eoi = false;
  for (size_t i = o >= 8 ? o - 8 : 0; i + 1 < o; i++) if (buf[i] == 0xFF && buf[i + 1] == 0xD9) { eoi = true; break; }
  if (!eoi) { Serial.printf("[%s] JPEG tronque (pas de EOI), %u o\n", tag, (unsigned)o); return false; }
  outLen = o;
  return true;
}

static const int SD_CS = 5, SD_SCK = 18, SD_MISO = 19, SD_MOSI = 23;
static SPIClass sdSPI(VSPI);
static bool     sdOk = false, sdBusDirty = false;
static uint32_t sdCount = 0;
static bool     sdCounted = false;
static uint32_t sdSizeMB = 0;
static const char* SD_DIR_S = "/np_s";
static const char* SD_DIR_B = "/np_b";

static uint8_t busOwner = 0;
static void sdBus() {
  if (busOwner == 1) return;
  sdSPI.end();
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  busOwner = 1; sdBusDirty = true;
}
static void touchBus() {
  if (busOwner == 0) return;
  touchSPI.end();
  touchSPI.begin(25, 39, 32, 33);
  busOwner = 0; sdBusDirty = false;
}

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
  sdCount = 0; sdCounted = false;
  Serial.printf("[sd] carte OK : %u Mo\n", (unsigned)sdSizeMB);
}

static void sdEnsureCount() {
  if (sdCounted || !sdOk) return;
  sdCounted = true; sdCount = 0;
  const char* dirs[2] = { SD_DIR_S, SD_DIR_B };
  for (int k = 0; k < 2; k++) {
    File d = SD.open(dirs[k]);
    if (!d) continue;
    File f = d.openNextFile();
    while (f) { sdCount++; f.close(); f = d.openNextFile(); }
    d.close();
  }
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
  sdEnsureCount();
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
  for (int pass = 0; pass < 40; pass++) {
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

static void sdDrop(const char* dir, const String& id) {
  if (!sdOk || !cfg.sdCache) return;
  String p = sdName(dir, id);
  if (!p.length()) return;
  sdBus();
  if (SD.exists(p) && SD.remove(p) && sdCount) sdCount--;
}

static void registerCoverFail(const String& id, const char* why) {
  if (id != coverFailId) { coverFailId = id; coverFails = 0; }
  if (coverFails < 255) coverFails++;
  coverFailMs = millis();
  Serial.printf("[cover] ECHEC %u/%u (%s) id=%s | free=%u maxBlk=%u\n", (unsigned)coverFails, (unsigned)COVER_MAX_FAILS,
                why, id.c_str(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

void drawWeatherIconLarge(int cx, int cy, int code) {
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
  if (!timeNow(&tm)) return;
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

static void fillDashData(ui::DashboardData& d) {
  struct tm tm;
  d.timeValid = timeNow(&tm);
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
    w.hasRange = idleData.forecastDays > 0;
    if (w.hasRange) {
      w.tMin = (int8_t)lroundf(idleData.forecast[0].tMin);
      w.tMax = (int8_t)lroundf(idleData.forecast[0].tMax);
    }
  }
  strlcpy(d.sunrise, idleData.hasData ? idleData.sunrise.c_str() : "", sizeof(d.sunrise));
  strlcpy(d.sunset,  idleData.hasData ? idleData.sunset.c_str()  : "", sizeof(d.sunset));
  d.ticker = (idleData.newsCount > 0 && newsFullStr.length()) ? newsFullStr.c_str()
                                                              : idleData.location.c_str();
}

void drawHomeScreen() {
  static ui::DashboardData d;
  fillDashData(d);
  g_dashboard.invalidate();
  drawModernDashboard(d);
  tft.setTextFont(1);
  tft.fillRect(215, 230, 105, 10, C_BG);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(RGB565(38,38,52), C_BG);
  tft.drawString(WiFi.localIP().toString(), SCR_W-4, IDLE_IP_Y, 1);
  tft.fillRect(0, 230, 215, 10, C_BG);
  if (idleData.newsCount > 0) buildNewsTicker();
  idleDrawn    = true;
  lastWifiDraw = 0;
}

void animateIdle() {
  if (!modernDashboardReady()) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry >= 2000UL) {
      lastTry = millis();
      if (initModernDashboard()) idleDrawn = false;
    }
    return;
  }
  if (!idleDrawn) { drawHomeScreen(); return; }
  static ui::DashboardData d;
  static uint32_t lastFill = 0;
  uint32_t now = millis();
  if (now - lastFill >= 100) { lastFill = now; fillDashData(d); }
  drawModernDashboard(d);
}

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
  tft.drawString(fitText(label, 2, 215 - x), x, 6, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(clockStr(), 280, 6, 2);
  drawStateIcon();
}

void drawButtons(int hi = -1) {
  for (int i = 0; i < 3; i++) {
    const int x = BTN_XS[i], w = BTN_WS[i], cx = x + w / 2, cy = BTN_Y + BTN_H / 2;
    const bool primary = (i == 1), pressed = (i == hi);
    const uint16_t bg = primary ? (pressed ? C_TEXT : C_ACCENT) : (pressed ? C_ACCENT : C_BTN);
    const uint16_t fg = (primary && pressed) ? C_ACCENT : C_TEXT;
    tft.fillRoundRect(x, BTN_Y, w, BTN_H, BTN_H / 2, bg);
    if (i == 0) {
      tft.fillTriangle(cx - 1, cy - 7, cx - 1, cy + 7, cx - 9, cy, fg);
      tft.fillTriangle(cx + 8, cy - 7, cx + 8, cy + 7, cx, cy, fg);
    } else if (i == 1) {
      if (np.state == "playing") {
        tft.fillRoundRect(cx - 7, cy - 9, 5, 18, 2, fg);
        tft.fillRoundRect(cx + 2, cy - 9, 5, 18, 2, fg);
      } else {
        tft.fillTriangle(cx - 5, cy - 10, cx - 5, cy + 10, cx + 9, cy, fg);
      }
    } else {
      tft.fillTriangle(cx - 8, cy - 7, cx - 8, cy + 7, cx, cy, fg);
      tft.fillTriangle(cx + 1, cy - 7, cx + 1, cy + 7, cx + 9, cy, fg);
    }
  }
}

void drawCoverPlaceholder(int x, int y, int sz) {
  tft.fillRoundRect(x, y, sz, sz, 10, C_BTN);
  const float k  = sz / 120.0f;
  const int   cx = x + sz / 2, cy = y + sz / 2;
  const int   r  = (int)(11 * k + 0.5f);
  const int   sw = max(3, (int)(4 * k + 0.5f));
  const int   lx = cx - (int)(20 * k), ly = cy + (int)(22 * k);
  const int   rx = cx + (int)(18 * k), ry = cy + (int)(14 * k);
  const int   tl = cy - (int)(30 * k), tr = cy - (int)(38 * k);
  tft.fillRect(lx + r - sw, tl, sw, ly - tl, C_DIM);
  tft.fillRect(rx + r - sw, tr, sw, ry - tr, C_DIM);
  tft.drawWideLine(lx + r - sw, tl + 3 * k, rx + r, tr + 3 * k, 8 * k, C_DIM, C_BTN);
  tft.fillSmoothCircle(lx, ly, r, C_DIM, C_BTN);
  tft.fillSmoothCircle(rx, ry, r, C_DIM, C_BTN);
}

static bool drawJpegSafe(int x, int y, const uint8_t* buf, size_t len, int maxDim, const char* tag) {
  if (!buf || len < 4) return false;
  uint16_t w = 0, h = 0;
  JRESULT r = TJpgDec.getJpgSize(&w, &h, buf, (uint32_t)len);
  if (r != JDR_OK || w == 0 || h == 0 || w > maxDim || h > maxDim) {
    Serial.printf("[%s] en-tete JPEG refuse : %s (%ux%u, %u o)\n", tag, diag::jpegErr((int)r), (unsigned)w, (unsigned)h, (unsigned)len);
    return false;
  }
  diag::HeapScope hs(tag);
  r = TJpgDec.drawJpg(x, y, buf, (uint32_t)len);
  if (r != JDR_OK) {
    Serial.printf("[%s] decodage echoue : %s\n", tag, diag::jpegErr((int)r));
    return false;
  }
  return true;
}

static void roundCorners(int x, int y, int sz, int r, uint16_t bg) {
  for (int i = 0; i < r; i++) {
    const float dy = r - i - 0.5f;
    const int off = (int)ceilf(r - sqrtf((float)r * r - dy * dy));
    if (off <= 0) continue;
    tft.drawFastHLine(x, y + i, off, bg);
    tft.drawFastHLine(x + sz - off, y + i, off, bg);
    tft.drawFastHLine(x, y + sz - 1 - i, off, bg);
    tft.drawFastHLine(x + sz - off, y + sz - 1 - i, off, bg);
  }
}

void drawCover() {
  const bool have = coverLen > 0 && np.coverId.length() && coverIdStored == np.coverId;
  if (have && drawJpegSafe(COVER_X, COVER_Y, coverBuf, coverLen, 240, "cover-draw")) { roundCorners(COVER_X, COVER_Y, COVER_SZ, 10, C_BG); return; }
  if (have) {
    const String bad = coverIdStored;
    registerCoverFail(bad, "decodage TJpgDec");
    sdDrop(SD_DIR_S, bad);
    coverLen = 0; coverIdStored = "";
  }
  drawCoverPlaceholder(COVER_X, COVER_Y, COVER_SZ);
}

void drawFullCoverScreen() {
  tft.fillScreen(C_BG);
  const bool bigOk = bigBuf && bigLen > 0 && np.coverId.length() && bigIdStored == np.coverId;
  if (bigOk) {
    if (drawJpegSafe((SCR_W - 240) / 2, 0, bigBuf, bigLen, 240, "big-draw")) return;
    bigLen = 0; bigIdStored = "";
  }
  const bool ok = coverLen > 0 && np.coverId.length() && coverIdStored == np.coverId;
  const int cx = (SCR_W - COVER_SZ) / 2, cy = (SCR_H - COVER_SZ) / 2;
  if (!(ok && drawJpegSafe(cx, cy, coverBuf, coverLen, 240, "cover-draw"))) drawCoverPlaceholder(cx, cy, COVER_SZ);
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
  tft.setTextColor(RGB565(99, 99, 102), C_BG);
  tft.drawString(line, COL_X, ALBUM_Y + 9);
  tft.setTextFont(1);
}

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
  barSpr.fillRoundRect(0, 5, BAR_W, 4, 2, C_TRACK);
  int fw = (np.duration > 0) ? (int)(BAR_W * pos / np.duration) : 0;
  if (fw > BAR_W) fw = BAR_W;
  if (fw >= 4)     barSpr.fillRoundRect(0, 5, fw, 4, 2, C_ACCENT);
  else if (fw > 0) barSpr.fillRect(0, 5, fw, 4, C_ACCENT);
  barSpr.fillSmoothCircle(constrain(fw, 5, BAR_W - 5), 7, 5, C_TEXT, C_BG);

  char a[12], b[12];
  fmtTime(sec, a, sizeof(a));
  if (np.duration > 0) fmtTime((int)np.duration, b, sizeof(b)); else strcpy(b, "--:--");
  barSpr.setTextColor(C_DIM, C_BG);
  barSpr.setTextDatum(TL_DATUM); barSpr.drawString(a, 0, 20, 2);
  barSpr.setTextDatum(TR_DATUM); barSpr.drawString(b, BAR_W, 20, 2);
  barSpr.pushSprite(BAR_X, BAR_Y);
}

void drawLyric() {
  const int LY_TOP = BAR_Y + BAR_H;
  const int LY_H   = SCR_H - LY_TOP;
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
    tft.drawString(l[0], SCR_W / 2, LY_TOP + (LY_H - LINE_H) / 2, 2);
  }
}

static int tftWidthN(const char* p, int n) {
  char b[160];
  if (n > (int)sizeof(b) - 1) n = (int)sizeof(b) - 1;
  memcpy(b, p, n); b[n] = 0;
  return tft.textWidth(b);
}
static void drawSlice(const char* s, const txt::Line& l, int x, int y) {
  char b[160];
  int n = l.len > (int)sizeof(b) - 1 ? (int)sizeof(b) - 1 : l.len;
  memcpy(b, s + l.start, n); b[n] = 0;
  tft.drawString(b, x, y);
}

struct LyFont { const GFXfont* gfx; uint8_t builtin; uint8_t lineH; uint8_t perPage; };
static const LyFont LY_FONTS[3] = {
  { &FreeSansBold12pt7b, 0, 28, 3 },
  { &FreeSansBold9pt7b,  0, 24, 4 },
  { nullptr,             2, 20, 5 },
};
static void applyLyFont(const LyFont& f) { if (f.gfx) tft.setFreeFont(f.gfx); else tft.setTextFont(f.builtin); }
static uint8_t lyricPages = 1;

void drawLyricFull() {
  tft.fillScreen(C_BG);
  const int CX = SCR_W / 2;

  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(fitText(np.title + " - " + np.artist, 1, SCR_W - 56), CX, 11, 1);
  tft.fillSmoothCircle(SCR_W - 16, 12, 10, C_BTN, C_BG);
  tft.drawLine(SCR_W - 20, 8, SCR_W - 12, 16, C_TEXT);
  tft.drawLine(SCR_W - 20, 16, SCR_W - 12, 8, C_TEXT);

  txt::Line ln[16];
  bool trunc = false;
  tft.setTextFont(2);
  tft.setTextColor(RGB565(99, 99, 102), C_BG);
  int pn = txt::wrap(np.lyricPrev.c_str(), SCR_W - 24, tftWidthN, ln, 2, &trunc);
  for (int i = 0; i < pn; i++) drawSlice(np.lyricPrev.c_str(), ln[i], CX, 62 - (pn - i) * 18 + 9);

  const char* curLine = np.lyric.length() ? np.lyric.c_str() : (lyricSeen ? "..." : "Pas de paroles");
  int total = 0, fi = 0;
  for (fi = 0; fi < 3; fi++) {
    applyLyFont(LY_FONTS[fi]);
    total = txt::wrap(curLine, SCR_W - 24, tftWidthN, ln, 16, &trunc);
    if (total <= LY_FONTS[fi].perPage && !trunc) break;
  }
  if (fi == 3) fi = 2;
  const LyFont& F = LY_FONTS[fi];
  lyricPages = (uint8_t)txt::pageCount(total, F.perPage);
  if (lyricPage >= lyricPages) lyricPage = 0;
  const int first = lyricPage * F.perPage;
  const int cnt   = (total - first) < F.perPage ? (total - first) : F.perPage;
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextDatum(MC_DATUM);
  const int y0 = 64 + (108 - cnt * F.lineH) / 2 + F.lineH / 2;
  for (int i = 0; i < cnt; i++) drawSlice(curLine, ln[first + i], CX, y0 + i * F.lineH);

  if (lyricPages > 1) {
    const int gap = 12, x0 = CX - (lyricPages - 1) * gap / 2;
    for (int p = 0; p < lyricPages; p++)
      tft.fillSmoothCircle(x0 + p * gap, 178, p == lyricPage ? 3 : 2, p == lyricPage ? C_ACCENT : C_TRACK, C_BG);
  }

  tft.setTextFont(2);
  tft.setTextColor(C_DIM, C_BG);
  int nn = txt::wrap(np.lyricNext.c_str(), SCR_W - 24, tftWidthN, ln, 2, &trunc);
  for (int i = 0; i < nn; i++) drawSlice(np.lyricNext.c_str(), ln[i], CX, 186 + i * 18 + 9);

  if (millis() < lyricHintUntil) {
    tft.setTextFont(1);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TRACK, C_BG);
    tft.drawString("Toucher pour fermer", CX, 228, 1);
  }
  tft.setTextFont(1);
  tickLyricBar(true);
}

void tickLyricBar(bool force) {
  static int lastS = -1;
  int s = (int)curPos();
  if (!force && s == lastS) return;
  lastS = s;
  const int y = SCR_H - 3;
  tft.fillRect(0, y, SCR_W, 3, C_TRACK);
  if (np.duration > 0) tft.fillRect(0, y, (int)(SCR_W * curPos() / np.duration), 3, C_ACCENT);
}

void renderLyricFull() {
  if (trackDirty) { trackDirty = false; coverDirty = false; lyricDirty = true; }
  if (lyricDirty) {
    lyricDirty = false; lyricPage = 0; lyricPageMs = millis();
    drawLyricFull();
  } else if (lyricPages > 1 && millis() - lyricPageMs > 2800) {
    lyricPage = (uint8_t)((lyricPage + 1) % lyricPages); lyricPageMs = millis();
    drawLyricFull();
  }
  if (lyricHintUntil && millis() > lyricHintUntil) {
    lyricHintUntil = 0;
    tft.fillRect(0, 226, SCR_W, 11, C_BG);
  }
  tickLyricBar();
}

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
    if (target >= h) h = target;
    else             h = max(target, h - max(3, (h - target) / 4));
    if (h > 0) allZero = false;

    int oldPeak = vzPeak[i], peak = oldPeak;
    if (h >= peak) { peak = h; vzPeakMs[i] = millis(); }
    else if (millis() - vzPeakMs[i] > 350) peak = max(h, peak - 2);

    if (h == vzH[i] && peak == oldPeak) continue;
    int x = VZ_X0 + i * bw, w = max(1, bw - 2);
    if (oldPeak > 0) tft.fillRect(x, VZ_BASE - oldPeak - 3, w, 3, C_BG);
    if (h > vzH[i])      tft.fillRect(x, VZ_BASE - h, w, h - vzH[i], C_ACCENT);
    else if (h < vzH[i]) tft.fillRect(x, VZ_BASE - vzH[i], w, vzH[i] - h, C_BG);
    if (peak > 0) tft.fillRect(x, VZ_BASE - peak - 3, w, 3, C_TEXT);
    vzH[i] = h; vzPeak[i] = peak;
  }
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
  volToastUntil = 0; volToastDirty = false;
  tft.fillScreen(C_BG);
  drawHeader();
  drawButtons();
  trackDirty = coverDirty = true;
  stateDirty = headerDirty = false;
  lastSec = -1;
  lyricDirty = true;
}

static void drawVolToast() {
  const int x = COL_X, y = 84, w = COL_W, h = 38;
  tft.fillRect(COL_X, ARTIST_Y - 2, COL_W, 48, C_BG);
  tft.fillRoundRect(x, y, w, h, 12, C_BTN);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(pcMute ? C_ACCENT : C_TEXT, C_BTN);
  tft.drawString(pcMute ? "Muet" : "Volume", x + 12, y + 12);
  if (!pcMute && pcVol >= 0) {
    char b[8]; snprintf(b, sizeof(b), "%d%%", pcVol);
    tft.setTextDatum(MR_DATUM); tft.setTextColor(C_DIM, C_BTN);
    tft.drawString(b, x + w - 12, y + 12);
  }
  tft.setTextFont(1);
  const int bx = x + 12, bw = w - 24, by = y + 27;
  tft.fillRoundRect(bx, by, bw, 4, 2, C_TRACK);
  const int fw = (pcVol > 0 && !pcMute) ? max(4, bw * pcVol / 100) : 0;
  if (fw > 0) tft.fillRoundRect(bx, by, fw, 4, 2, C_ACCENT);
}

void renderPlay() {
  if (fullCover) {
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
  if (volToastDirty) { volToastDirty = false; if (volToastUntil) drawVolToast(); }
  if (volToastUntil && millis() > volToastUntil) { volToastUntil = 0; drawArtistAlbum(); }
  tickTitle();
  tickBar();
}

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
  static int prevH[5] = { 0, 0, 0, 0, 0 };
  for (int i = 0; i < 5; i++) {
    const int h  = 10 + (int)(24 + 24 * sinf(millis() / 260.0f + i * 1.3f));
    const int x  = 110 + 6 + i * 20, yb = 30 + 70;
    if (h < prevH[i]) tft.fillRect(x, yb - prevH[i], 10, prevH[i] - h + 1, C_BG);
    tft.fillRoundRect(x, yb - h, 10, h, 3, C_ACCENT);
    prevH[i] = h;
  }
  struct tm tm;
  if (timeNow(&tm)) {
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

bool autoDimReset = true;
bool screenOff = false;
void tickAutoDim() {
  if (screenOff) return;
  static bool dimmed = false;
  static int  lastPwm = -1;

  static bool     isNight = false;
  static uint32_t lastNightChk = 0;
  if (!cfg.nightOn) {
    isNight = false;
  } else if (autoDimReset || millis() - lastNightChk >= 1000UL) {
    lastNightChk = millis();
    struct tm tm;
    if (timeNow(&tm)) {
      const int h = tm.tm_hour;
      if (cfg.nightStart == cfg.nightEnd)      isNight = false;
      else if (cfg.nightStart < cfg.nightEnd)  isNight = (h >= cfg.nightStart && h < cfg.nightEnd);
      else                                     isNight = (h >= cfg.nightStart || h < cfg.nightEnd);
    }
  }

  const uint8_t nightPwm = (uint8_t)max(3, (int)cfg.nightBri * 255 / 100);
  const uint8_t dimPwm   = (uint8_t)max(3, (int)cfg.dimBri   * 255 / 100);
  uint8_t normPwm = pctToPwm(cfg.briPct);
  if (cfg.autoBri) {
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

void pressButton(int i) {
  static const char* cmds[3] = { "prev", "toggle", "next" };
  pendingCmd = cmds[i];
  cmdMs = millis();
  flashIdx = i;
  flashUntil = millis() + 180;
  drawButtons(i);
}

static const int HIT_BTN_Y0  = BTN_Y - 16, HIT_BTN_Y1 = BTN_Y + BTN_H + 2;
static const int HIT_SEEK_Y0 = HIT_BTN_Y1 + 1, HIT_SEEK_Y1 = BAR_Y + 32;
static const int HIT_LYR_Y0  = HIT_SEEK_Y1 + 1;

static int playButtonHit(int x, int y) {
  if (y < HIT_BTN_Y0 || y > HIT_BTN_Y1 || x < COL_X - 10) return -1;
  if (x < BTN_XS[1] - 5) return 0;
  if (x < BTN_XS[2] - 5) return 1;
  return 2;
}

static void onPlayRelease(int sx, int sy, int ex, int ey) {
  int dx = ex - sx, dy = ey - sy;
  if (fullCover) { fullCover = false; bigRelease(); drawMusicScreen(); return; }
  if (vizMode)   { vizMode   = false; drawMusicScreen(); return; }
  if (lyricFull) { lyricFull = false; lyricHintUntil = 0; drawMusicScreen(); return; }
  if (sy >= HIT_SEEK_Y0 && sy <= HIT_SEEK_Y1 && np.duration > 0) {
    float frac = constrain((float)(ex - BAR_X) / (float)BAR_W, 0.0f, 1.0f);
    float secs = frac * np.duration;
    pendingCmd = "seek:" + String(secs, 1);
    cmdMs = millis();
    basePos = secs; baseMs = millis();
    tickBar(true);
    return;
  }
  if (abs(dx) > 40 && abs(dx) > abs(dy)) {
    pressButton(dx > 0 ? 0 : 2);
    return;
  }
  if (abs(dy) > 40 && abs(dy) > abs(dx) && sx >= COL_X && sy < HIT_BTN_Y1) {
    pendingCmd = dy < 0 ? "volup" : "voldown";
    cmdMs = millis();
    volToastUntil = millis() + 2500; volToastDirty = true;
    return;
  }
  if (abs(dx) > 18 || abs(dy) > 18) return;

  if (sx >= COVER_X && sx <= COVER_X + COVER_SZ && sy >= COVER_Y && sy <= COVER_Y + COVER_SZ) {
    fullCover = true; drawFullCoverScreen(); return;
  }
  if (volToastUntil && millis() < volToastUntil && sx >= COL_X && sy >= 80 && sy <= 124) {
    pendingCmd = "mute"; cmdMs = millis();
    volToastUntil = millis() + 2500; volToastDirty = true;
    return;
  }
  if (sx >= COL_X && sy >= ARTIST_Y - 6 && sy < HIT_BTN_Y0) {
    vizMode = true; drawVizScreen(); return;
  }
  if (sy >= HIT_LYR_Y0 && (lyricSeen || np.lyric.length() || np.lyricPrev.length() || np.lyricNext.length())) {
    lyricFull = true; lyricDirty = false;
    lyricPage = 0; lyricPageMs = millis(); lyricHintUntil = millis() + 3000;
    drawLyricFull();
    return;
  }
}

static const int NEWS_HEAD_H = 34, NEWS_Y0 = 38, NEWS_PITCH = 38;
static int newsRowHit(int x, int y) {
  (void)x;
  const int n = idleData.newsCount < 5 ? idleData.newsCount : 5;
  const int top = NEWS_Y0 - 3;
  if (y < top || y >= top + n * NEWS_PITCH) return -1;
  return (y - top) / NEWS_PITCH;
}

void sysTouchDown(int x, int y);

void handleTouch() {
  static bool     down   = false;
  static uint32_t lastRelease = 0;
  static int      startX = -1, startY = -1;
  static int      lastX  = -1, lastY  = -1;
  static uint32_t lastIdleTap = 0;

  static uint32_t lastSeen = 0;
  touchBus();
  if (!ts.touched()) {
    if (down && millis() - lastSeen < 40) return;
    if (down) {
      lastRelease = millis();
      if (cur == SCR_PLAY && startX >= 0) onPlayRelease(startX, startY, lastX, lastY);
      if (cur == SCR_MENU) menuTouchUp(lastX);
    }
    down = false; startX = startY = -1;
    return;
  }

  lastSeen = lastTouchMs = millis();
  {
    TS_Point p0 = ts.getPoint();
    lastX = constrain(map(p0.x, 200, 3700, 1, SCR_W), 0, SCR_W - 1);
    lastY = constrain(map(p0.y, 240, 3800, 1, SCR_H), 0, SCR_H - 1);
  }

  if (screenOff) {
    screenOff = false; autoDimReset = true; down = true; startX = startY = -1;
    if (cur == SCR_MENU) { cur = SCR_WAIT; idleDrawn = false; }
    return;
  }

  if (down && cur == SCR_PLAY && startX >= 0 && !fullCover && !vizMode && !lyricFull &&
      startY >= HIT_SEEK_Y0 && startY <= HIT_SEEK_Y1 && np.duration > 0 && abs(lastX - startX) > 6) {
    static uint32_t lastPreview = 0;
    if (millis() - lastPreview > 60) {
      lastPreview = millis();
      basePos = constrain((float)(lastX - BAR_X) / (float)BAR_W, 0.0f, 1.0f) * np.duration;
      baseMs = millis();
      tickBar(true);
    }
  }

  if (down && cur == SCR_MENU) menuTouchDrag(lastX, lastY);

  if (down || millis() - lastRelease < 30) return;
  down = true;

  int x = lastX, y = lastY;

  if (cur == SCR_MENU) { menuTouchDown(x, y); return; }

  if (cur == SCR_SYS) { sysTouchDown(x, y); return; }

  if (cur == SCR_IDLE_WX) {
    if (wxDetailDay >= 0) { wxDetailDay = -1; drawForecastScreen(); return; }
    if (x < 80 && y < 38) { cur = SCR_WAIT; idleDrawn = false; return; }
    if (y >= 198) { wxTapTab(x); return; }
    const int col = fcHit(x, y);
    if (col >= 0) { wxDetailDay = col; drawDayDetailScreen(col); }
    return;
  }

  if (cur == SCR_IDLE_NEWS) {
    if (newsDetailIdx >= 0) { newsDetailIdx = -1; drawNewsScreen(); return; }
    int row = newsRowHit(x, y);
    if (row >= 0) { drawNewsDetailScreen(row); return; }
    if (y < NEWS_HEAD_H) { cur = SCR_WAIT; idleDrawn = false; return; }
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

  if (cur == SCR_WAIT && x > SCR_W - 48 && y < 36) {
    cur = SCR_MENU; drawMenuScreen(); return;
  }
  if (cur == SCR_WAIT) {
    uint32_t now = millis();
    if (now - lastIdleTap < 500 && hasTrack) {
      forcedIdle = false; pausedSince = 0; cur = SCR_NONE;
      lastIdleTap = 0; return;
    }
    lastIdleTap = now;
    if (y >= IDLE_NEWS_BAND && y <= IDLE_NEWS_BAND + IDLE_NEWS_H) {
      cur = SCR_IDLE_NEWS; newsDetailIdx = -1; drawNewsScreen(); return;
    }
    if (y >= IDLE_WX_Y && y <= IDLE_WX_Y + IDLE_WX_H) {
      cur = SCR_IDLE_WX; drawForecastScreen(); return;
    }
    return;
  }

  if (cur != SCR_PLAY) return;

  startX = x; startY = y;
  if (fullCover || vizMode || lyricFull) return;

  int bi = playButtonHit(x, y);
  if (bi >= 0) { startX = -1; pressButton(bi); return; }
}

void sendReply(bool needCover, bool needIcon, bool needBig = false) {
  JsonDocument r;
  r["ok"] = true;
  r["needCover"] = needCover;
  r["needIcon"] = needIcon;
  r["needBig"] = needBig;
  r["viz"] = wantViz();
  if (pendingCmd.length()) {
    if (millis() - cmdMs < 4000) r["cmd"] = pendingCmd;
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
    coverFails = 0; coverFailId = "";
    lyricSeen = false; lyricPage = 0;
  }
  if (app != np.app || appId != np.appId) { np.app = app; np.appId = appId; headerDirty = true; }
  if (np.state != st) {
    np.state = st;
    stateDirty = true;
    if (st == "paused" && pausedSince == 0) pausedSince = millis();
    else if (st == "playing")               { pausedSince = 0; forcedIdle = false; }
  }
  {
    String ac = d["accent"] | "";
    uint32_t na = (ac.length() == 7 && ac[0] == '#') ? ((uint32_t)strtoul(ac.c_str() + 1, nullptr, 16) & 0xFFFFFF) : 0;
    if (na != dynAccent) { dynAccent = na; applyAccent(); stateDirty = true; headerDirty = true; }
  }
  {
    String ly = d["lyric"] | "", lp = d["lyricPrev"] | "", ln = d["lyricNext"] | "";
    const int lin = d["lyricIn"] | 0;
    lyricSwapAt = (lin > 0 && lin < 30000 && ln.length()) ? millis() + (uint32_t)lin : 0;
    if (ly != np.lyric || lp != np.lyricPrev || ln != np.lyricNext) {
      np.lyric = ly; np.lyricPrev = lp; np.lyricNext = ln; lyricDirty = true;
      lyricPage = 0; lyricPageMs = millis();
      if (ly.length() || lp.length() || ln.length()) lyricSeen = true;
    }
  }
  np.duration = d["duration"] | 0.0f;
  {
    const int v = d["vol"] | -1; const bool m = d["mute"] | false;
    if (v != pcVol || m != pcMute) { pcVol = v; pcMute = m; if (volToastUntil && millis() < volToastUntil) volToastDirty = true; }
  }
  basePos = d["position"] | 0.0f;
  baseMs = millis();
  hasTrack = true;

  bool needCover = cid.length() > 0 && cid != coverIdStored;
  if (needCover && sdLoad(SD_DIR_S, cid, coverBuf, COVER_MAX_BYTES, coverLen)) {
    coverIdStored = cid; coverDirty = true; needCover = false;
  }
  if (needCover && cid == coverFailId && coverFails >= COVER_MAX_FAILS && millis() - coverFailMs < 30000UL) needCover = false;
  bool needBig = false;
  if (fullCover && cid.length() > 0 && cid != bigIdStored && bigEnsure()) {
    if (sdLoad(SD_DIR_B, cid, bigBuf, BIG_MAX_BYTES, bigLen)) { bigIdStored = cid; bigDirty = true; }
    else needBig = true;
  }
  sendReply(needCover, cfg.showApp && appId.length() > 0 && appId != iconIdStored, needBig);
}

enum ImgKind : uint8_t { IMG_COVER, IMG_ICON, IMG_BIG };
struct ImgRx { uint8_t* buf; size_t cap; size_t len; bool ok; bool streamed; };
static ImgRx imgRx = { nullptr, 0, 0, false, false };

static void imgUpload(ImgKind kind) {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    imgRx.streamed = true; imgRx.len = 0; imgRx.ok = true;
    if      (kind == IMG_COVER) { imgRx.buf = coverBuf; imgRx.cap = COVER_MAX_BYTES; coverLen = 0; }
    else if (kind == IMG_ICON)  { imgRx.buf = iconBuf;  imgRx.cap = ICON_MAX_BYTES;  iconLen  = 0; }
    else                        { bigLen = 0; imgRx.buf = bigEnsure() ? bigBuf : nullptr; imgRx.cap = BIG_MAX_BYTES; }
    if (!imgRx.buf) imgRx.ok = false;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!imgRx.ok) return;
    if (imgRx.len + up.currentSize > imgRx.cap) { imgRx.ok = false; Serial.printf("[img] trop gros (> %u o)\n", (unsigned)imgRx.cap); return; }
    memcpy(imgRx.buf + imgRx.len, up.buf, up.currentSize);
    imgRx.len += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    bool eoi = false;
    if (imgRx.ok && imgRx.len >= 4 && imgRx.buf[0] == 0xFF && imgRx.buf[1] == 0xD8)
      for (size_t i = imgRx.len >= 8 ? imgRx.len - 8 : 0; i + 1 < imgRx.len; i++)
        if (imgRx.buf[i] == 0xFF && imgRx.buf[i + 1] == 0xD9) { eoi = true; break; }
    if (!eoi) { Serial.printf("[img] JPEG invalide/tronque (%u o)\n", (unsigned)imgRx.len); imgRx.ok = false; }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    imgRx.ok = false;
  }
}
static void uploadCover() { imgUpload(IMG_COVER); }
static void uploadIcon()  { imgUpload(IMG_ICON); }
static void uploadBig()   { imgUpload(IMG_BIG); }

static bool imgStreamResult(bool& ok, size_t& len) {
  if (!imgRx.streamed) return false;
  imgRx.streamed = false; ok = imgRx.ok; len = imgRx.len;
  return true;
}

void handleCover() {
  diag::HeapScope hs("cover-rx");
  const String id = server.arg("id");
  size_t n = 0; bool ok = false;
  if (!imgStreamResult(ok, n)) { coverLen = 0; ok = decodeJpegBody(coverBuf, COVER_MAX_BYTES, n, "cover"); }
  if (!ok) {
    coverLen = 0; coverIdStored = "";
    registerCoverFail(id, "reception");
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  coverLen = n; coverIdStored = id;
  coverFails = 0; coverFailId = "";
  coverDirty = true;
  server.send(200, "application/json", "{\"ok\":true}");
  sdSave(SD_DIR_S, coverIdStored, coverBuf, coverLen);
}

void handleBigCover() {
  diag::HeapScope hs("coverbig-rx");
  const String id = server.arg("id");
  size_t n = 0; bool ok = false;
  if (!imgStreamResult(ok, n)) { bigLen = 0; ok = bigEnsure() && decodeJpegBody(bigBuf, BIG_MAX_BYTES, n, "big"); }
  if (!ok) {
    bigLen = 0; bigIdStored = "";
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  bigLen = n; bigIdStored = id; bigDirty = true;
  server.send(200, "application/json", "{\"ok\":true}");
  sdSave(SD_DIR_B, bigIdStored, bigBuf, bigLen);
}

void handleIcon() {
  size_t n = 0; bool ok = false;
  if (!imgStreamResult(ok, n)) { iconLen = 0; ok = decodeJpegBody(iconBuf, ICON_MAX_BYTES, n); }
  if (!ok) { iconLen = 0; server.send(400, "application/json", "{\"ok\":false}"); return; }
  iconLen = n;
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
  const uint32_t oldAccent = cfg.accent;
  const uint16_t oldScroll = cfg.scroll;
  const bool     oldShowApp = cfg.showApp;
  if (!d["brightness"].isNull()) cfg.briPct = (uint8_t)constrain(d["brightness"].as<int>(), 5, 100);
  if (!d["autoBri"].isNull())    cfg.autoBri    = d["autoBri"].as<bool>();
  if (!d["ledMode"].isNull())    cfg.ledMode    = (uint8_t)constrain(d["ledMode"].as<int>(), 0, 3);
  if (!d["ledBri"].isNull())     cfg.ledBri     = (uint8_t)constrain(d["ledBri"].as<int>(), 5, 100);
  if (!d["ledNightOff"].isNull()) cfg.ledNightOff = d["ledNightOff"].as<bool>();
  if (!d["sdCache"].isNull())    cfg.sdCache    = d["sdCache"].as<bool>();
  if (!d["sdMax"].isNull())      cfg.sdMax      = (uint16_t)constrain(d["sdMax"].as<int>(), 20, 1000);
  bool calOnly = false;
  if (!d["ldrCal"].isNull()) {
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
  if (!calOnly && (cfg.accent != oldAccent || cfg.scroll != oldScroll || cfg.showApp != oldShowApp)) cur = SCR_NONE;
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
              : (cur == SCR_SYS) ? "system" : (cur == SCR_MENU) ? "menu" : "wifi";
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
  d["chip"]["model"] = ESP.getChipModel();
  d["chip"]["rev"]   = ESP.getChipRevision();
  d["chip"]["cores"] = ESP.getChipCores();
  d["chip"]["sdk"]   = ESP.getSdkVersion();
  d["reset"]         = resetReasonText();
  const diag::Heap hp = diag::snap();
  d["heapInfo"]["total"]   = ESP.getHeapSize();
  d["heapInfo"]["minFree"] = ESP.getMinFreeHeap();
  d["heapInfo"]["fragPct"] = diag::fragPct(hp);
  d["flash"]["size"]       = ESP.getFlashChipSize();
  d["flash"]["mhz"]        = ESP.getFlashChipSpeed() / 1000000UL;
  d["flash"]["sketch"]     = ESP.getSketchSize();
  d["flash"]["freeSketch"] = ESP.getFreeSketchSpace();
  d["wifi"]["ssid"]        = WiFi.SSID();
  d["wifi"]["channel"]     = WiFi.channel();
  d["loop"]["avgUs"]       = loopStat.avgUs;
  d["loop"]["worstUs"]     = loopStat.worstUs;
  d["loop"]["hz"]          = loopStat.hz;
  d["loop"]["slow"]        = loopStat.slowLast;
  d["loop"]["slowTotal"]   = loopStat.slowTotal;
  d["stackFree"]           = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void handleSdClear() {
  sdClear();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleIdle() {
  diag::HeapScope hs("rss-parse");
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
  bool hwField = d["hasWeather"] | false;
  idleData.hasData     = hwField || idleData.weatherCode >= 0;

  idleData.newsCount = 0;
  for (int i = 0; i < 10; i++) { idleData.newsMeta[i].desc = ""; idleData.newsMeta[i].when = ""; idleData.newsMeta[i].src = ""; }
  JsonArray items = d["newsItems"].as<JsonArray>();
  if (!items.isNull() && items.size() > 0) {
    for (JsonObject it : items) {
      if (idleData.newsCount >= 10) break;
      String t = cleanUTF8(String((const char*)(it["t"] | "")));
      if (!t.length()) continue;
      const int k = idleData.newsCount++;
      idleData.news[k] = t;
      String desc = cleanUTF8(String((const char*)(it["d"] | "")));
      if (desc.length() > NEWS_DESC_MAX) desc.remove(NEWS_DESC_MAX);
      idleData.newsMeta[k].desc = desc;
      idleData.newsMeta[k].when = cleanUTF8(String((const char*)(it["p"] | "")));
      idleData.newsMeta[k].src  = cleanUTF8(String((const char*)(it["s"] | "")));
    }
  } else {
    JsonArray arr = d["news"].as<JsonArray>();
    for (JsonVariant v : arr) {
      if (idleData.newsCount >= 10) break;
      idleData.news[idleData.newsCount++] = cleanUTF8(v.as<String>());
    }
  }

  Serial.printf("[idle] temp=%.1f code=%d hasData=%d loc=%s news=%d\n",
    idleData.temp, idleData.weatherCode, (int)idleData.hasData,
    idleData.location.c_str(), idleData.newsCount);

  if (idleData.newsCount > 0) buildNewsTicker();
  if (cur == SCR_WAIT) {
  } else if (cur == SCR_IDLE_WX && wxDetailDay < 0) {
    drawForecastScreen();
  } else if (cur == SCR_IDLE_NEWS) {
    if (newsDetailIdx >= 0) {
      if (newsDetailIdx >= idleData.newsCount || idleData.news[newsDetailIdx] != newsDetailTitle) { newsDetailIdx = -1; drawNewsScreen(); }
    } else {
      drawNewsScreen();
    }
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

static void ellipsizeOn(TFT_eSPI& g, const char* in, char* out, size_t cap, int maxW) {
  strlcpy(out, in, cap - 3);
  if (g.textWidth(out) <= maxW) return;
  const int dotsW = g.textWidth("...");
  size_t n = strlen(out);
  while (n > 0 && g.textWidth(out) + dotsW > maxW) out[--n] = 0;
  while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
  { size_t m = strlen(out); snprintf(out + m, cap - m, "..."); }
}

static inline void ellipsize(const char* in, char* out, size_t cap, int maxW) { ellipsizeOn(tft, in, out, cap, maxW); }

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

static uint8_t wxTab = 0;
static const int WX_TAB_Y = 204, WX_TAB_H = 30, WX_CY0 = 116, WX_CH = 84;
static const int FC_N = 5, FC_W = 58, FC_GAP = 4, FC_Y = WX_CY0, FC_H = WX_CH;
static inline int fcCount()        { return idleData.forecastDays < FC_N ? idleData.forecastDays : FC_N; }
static inline int fcX0(int n)      { return (SCR_W - (n * FC_W + (n - 1) * FC_GAP)) / 2; }

int fcHit(int x, int y) {
  const int n = fcCount();
  if (wxTab != 1 || n <= 0 || y < FC_Y || y > FC_Y + FC_H) return -1;
  const int rel = x - fcX0(n);
  if (rel < 0) return -1;
  const int i = rel / (FC_W + FC_GAP);
  return i < n ? i : -1;
}

static const char* uvWord(float uv) {
  return uv < 0 ? "--" : uv < 3 ? "Faible" : uv < 6 ? "Modere" : uv < 8 ? "Fort" : uv < 11 ? "Tres fort" : "Extreme";
}
static uint16_t uvColor(float uv) {
  return uv < 3 ? RGB565(48, 209, 88) : uv < 6 ? ui::C_AMBER : RGB565(255, 69, 58);
}

static void wxDrawHeader() {
  tft.fillRect(0, 0, SCR_W, 36, C_BG);
  tft.drawWideLine(22, 10, 14, 17, 2.5f, C_TEXT, C_BG);
  tft.drawWideLine(14, 17, 22, 24, 2.5f, C_TEXT, C_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, C_BG);
  char buf[64];
  ellipsize(idleData.location.length() ? idleData.location.c_str() : "Meteo", buf, sizeof(buf), 170);
  tft.drawString(buf, 34, 17);
  if (idleData.hasData && idleData.uvIndex >= 0) {
    char u[12]; snprintf(u, sizeof(u), "UV %d", (int)lroundf(idleData.uvIndex));
    tft.setFreeFont(&FreeSans9pt7b);
    const int pw = tft.textWidth(u) + 18;
    const uint16_t col = uvColor(idleData.uvIndex);
    tft.fillRoundRect(310 - pw, 6, pw, 22, 11, col);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(RGB565(16, 16, 20), col);
    tft.drawString(u, 310 - pw / 2, 17);
  }
  tft.setTextFont(1);
}

static void wxDrawNow() {
  tft.fillRoundRect(10, 38, 300, 74, ui::R_CARD, ui::C_CARD);
  tft.setFreeFont(&FreeSans9pt7b);
  if (!idleData.hasData) {
    tft.setTextDatum(MC_DATUM); tft.setTextColor(ui::C_MUTED, ui::C_CARD);
    tft.drawString("Meteo indisponible", SCR_W / 2, 75);
    tft.setTextFont(1);
    return;
  }
  char buf[64];
  ui::drawWxIcon(tft, 44, 74, ui::wmoToIcon(idleData.weatherCode), 0.85f, ui::C_CARD);
  tft.setFreeFont(&FreeSansBold24pt7b);
  drawTemp(80, 66, (int)lroundf(idleData.temp), C_TEXT, ui::C_CARD, 5, 2, -13, 0);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(ui::C_MUTED, ui::C_CARD); tft.setTextDatum(ML_DATUM);
  ellipsize(idleData.description.c_str(), buf, sizeof(buf), 105);
  tft.drawString(buf, 80, 98);
  tft.drawString("Ressenti", 196, 56);
  tft.drawString("Vent",     196, 76);
  tft.drawString("Humid.",   196, 96);
  tft.setTextColor(C_TEXT, ui::C_CARD);
  drawTemp(298, 56, (int)lroundf(idleData.feelsLike), C_TEXT, ui::C_CARD, 3, 1, -5, 2);
  tft.setTextDatum(MR_DATUM); tft.setTextColor(C_TEXT, ui::C_CARD);
  snprintf(buf, sizeof(buf), "%d km/h", (int)lroundf(idleData.windSpeed));
  tft.drawString(buf, 298, 76);
  snprintf(buf, sizeof(buf), "%d %%", (int)lroundf(idleData.humidity));
  tft.drawString(buf, 298, 96);
  tft.setTextFont(1);
}

static void wxEmptyMsg(const char* msg) {
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(ui::C_MUTED, C_BG); tft.setTextDatum(MC_DATUM);
  tft.drawString(msg, SCR_W / 2, WX_CY0 + WX_CH / 2);
  tft.setTextFont(1);
}

static void wxDrawHours() {
  if (idleData.hourlyN < 2) { wxEmptyMsg("Previsions horaires en attente..."); return; }
  const int W = 46, G = 4, X0 = 12;
  for (int i = 0; i < 6; i++) {
    const int k = i * 2;
    if (k >= idleData.hourlyN) break;
    const HourFc& h = idleData.hourly[k];
    const int x = X0 + i * (W + G), cx = x + W / 2;
    const uint16_t bg = (i == 0) ? ui::C_CARD2 : ui::C_CARD;
    tft.fillRoundRect(x, WX_CY0, W, WX_CH, 10, bg);
    char b[8];
    snprintf(b, sizeof(b), "%dh", (int)h.hour);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(i == 0 ? ui::C_AMBER : ui::C_MUTED, bg);
    tft.drawString(b, cx, WX_CY0 + 13);
    ui::drawWxIcon(tft, cx, WX_CY0 + 37, ui::wmoToIcon(h.code), 0.5f, bg);
    tft.setFreeFont(&FreeSansBold9pt7b);
    drawTemp(cx, WX_CY0 + 60, (int)h.t, C_TEXT, bg, 2, 1, -5, 1);
    if (h.pop >= 10) {
      tft.setTextFont(1);
      snprintf(b, sizeof(b), "%d%%", (int)h.pop);
      tft.setTextDatum(MC_DATUM); tft.setTextColor(ui::C_BLUE, bg);
      tft.drawString(b, cx, WX_CY0 + 75);
    }
  }
  tft.setTextFont(1);
}

static void wxDrawDays() {
  static const char* const DAY3[7] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
  const int n = fcCount();
  if (n <= 0) { wxEmptyMsg("Previsions en attente..."); return; }
  struct tm tnow; timeNow(&tnow);
  const int x0 = fcX0(n);
  for (int i = 0; i < n; i++) {
    const int x = x0 + i * (FC_W + FC_GAP), cx = x + FC_W / 2;
    const uint16_t bg = (i == 0) ? ui::C_CARD2 : ui::C_CARD;
    tft.fillRoundRect(x, FC_Y, FC_W, FC_H, 10, bg);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(i == 0 ? ui::C_AMBER : ui::C_MUTED, bg);
    tft.drawString(i == 0 ? "AUJ." : DAY3[(tnow.tm_wday + i) % 7], cx, FC_Y + 12);
    ui::drawWxIcon(tft, cx, FC_Y + 34, ui::wmoToIcon(idleData.forecast[i].code), 0.6f, bg);
    drawTemp(cx, FC_Y + 56, (int)lroundf(idleData.forecast[i].tMax), C_TEXT, bg, 2, 1, -5, 1);
    tft.setFreeFont(&FreeSans9pt7b);
    drawTemp(cx, FC_Y + 72, (int)lroundf(idleData.forecast[i].tMin), ui::C_MUTED, bg, 2, 1, -5, 1);
  }
  tft.setTextFont(1);
}

static void wxTile(int col, int row, const char* label, const char* value, uint16_t vcol) {
  const int W = 96, H = 40, x = 12 + col * (W + 6), y = WX_CY0 + row * (H + 4);
  tft.fillRoundRect(x, y, W, H, 10, ui::C_CARD);
  tft.setTextFont(1); tft.setTextDatum(TL_DATUM); tft.setTextColor(ui::C_MUTED, ui::C_CARD);
  tft.drawString(label, x + 10, y + 7);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(vcol, ui::C_CARD);
  tft.drawString(value, x + 10, y + 27);
  tft.setTextFont(1);
}

static void wxDrawDetails() {
  if (!idleData.hasData) { wxEmptyMsg("Meteo indisponible"); return; }
  char b[24];
  if (idleData.uvIndex >= 0) snprintf(b, sizeof(b), "%d %s", (int)lroundf(idleData.uvIndex), uvWord(idleData.uvIndex));
  else                       strcpy(b, "--");
  wxTile(0, 0, "UV", b, idleData.uvIndex >= 0 ? uvColor(idleData.uvIndex) : C_TEXT);
  snprintf(b, sizeof(b), "%d km/h", (int)lroundf(idleData.windSpeed));
  wxTile(1, 0, "VENT", b, C_TEXT);
  snprintf(b, sizeof(b), "%d %%", (int)lroundf(idleData.humidity));
  wxTile(2, 0, "HUMIDITE", b, C_TEXT);
  if (idleData.pressure > 0) snprintf(b, sizeof(b), "%d hPa", (int)lroundf(idleData.pressure)); else strcpy(b, "--");
  wxTile(0, 1, "PRESSION", b, C_TEXT);
  wxTile(1, 1, "LEVER",   idleData.sunrise.length() ? idleData.sunrise.c_str() : "--", ui::C_AMBER);
  wxTile(2, 1, "COUCHER", idleData.sunset.length()  ? idleData.sunset.c_str()  : "--", ui::C_AMBER);
}

static void wxDrawContent() {
  if (wxTab == 0)      wxDrawHours();
  else if (wxTab == 1) wxDrawDays();
  else                 wxDrawDetails();
}

static void wxDrawTabs() {
  static const char* const NAMES[3] = { "Heures", "Jours", "Details" };
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < 3; i++) {
    const int x = 10 + i * 102;
    const uint16_t bg = (i == wxTab) ? C_ACCENT : ui::C_CARD;
    tft.fillRoundRect(x, WX_TAB_Y, 96, WX_TAB_H, 10, bg);
    tft.setTextColor(i == wxTab ? C_TEXT : ui::C_MUTED, bg);
    tft.drawString(NAMES[i], x + 48, WX_TAB_Y + WX_TAB_H / 2);
  }
  tft.setTextFont(1);
}

void drawForecastScreen() {
  tft.fillScreen(C_BG);
  wxDrawHeader();
  wxDrawNow();
  wxDrawContent();
  wxDrawTabs();
}

void wxTapTab(int x) {
  const uint8_t t = x < 108 ? 0 : (x < 214 ? 1 : 2);
  if (t == wxTab) return;
  wxTab = t;
  tft.fillRect(0, WX_CY0 - 2, SCR_W, WX_TAB_Y - WX_CY0 + 2, C_BG);
  wxDrawContent();
  wxDrawTabs();
}

void drawNewsScreen() {
  tft.fillScreen(C_BG);

  tft.drawWideLine(22, 10, 14, 17, 2.5f, C_TEXT, C_BG);
  tft.drawWideLine(14, 17, 22, 24, 2.5f, C_TEXT, C_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("Actualites", 34, 17);

  tft.setFreeFont(&FreeSans9pt7b);
  if (idleData.newsCount == 0) {
    tft.setTextColor(ui::C_MUTED, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Aucune actualite disponible", SCR_W / 2, 112);
    tft.setTextFont(1);
    tft.setTextColor(RGB565(90, 90, 98), C_BG);
    tft.drawString("Choisis un flux RSS dans l'appli PC", SCR_W / 2, 134);
  } else {
    const int ROW_H = 35, MAXR = 5, TXT_X = 28, TXT_W = 300 - (TXT_X - 10) - 24;
    const int n = idleData.newsCount < MAXR ? idleData.newsCount : MAXR;
    char line[112], meta[72];
    for (int i = 0; i < n; i++) {
      const int y = NEWS_Y0 + i * NEWS_PITCH;
      tft.fillRoundRect(10, y, 300, ROW_H, 10, ui::C_CARD);
      tft.fillRoundRect(18, y + 8, 3, ROW_H - 16, 1, C_ACCENT);
      const NewsMeta& m = idleData.newsMeta[i];
      meta[0] = 0;
      if (m.src.length() && m.when.length()) snprintf(meta, sizeof(meta), "%s - %s", m.src.c_str(), m.when.c_str());
      else if (m.src.length())               snprintf(meta, sizeof(meta), "%s", m.src.c_str());
      else if (m.when.length())              snprintf(meta, sizeof(meta), "%s", m.when.c_str());
      tft.setTextFont(1);
      if (meta[0]) {
        char mb[72]; ellipsizeOn(tft, meta, mb, sizeof(mb), TXT_W);
        tft.setTextDatum(TL_DATUM); tft.setTextColor(ui::C_MUTED, ui::C_CARD);
        tft.drawString(mb, TXT_X, y + 5);
      }
      tft.setFreeFont(&FreeSans9pt7b);
      ellipsize(idleData.news[i].c_str(), line, sizeof(line), TXT_W);
      tft.setTextColor(C_TEXT, ui::C_CARD);
      tft.setTextDatum(ML_DATUM);
      tft.drawString(line, TXT_X, y + (meta[0] ? 23 : 18));
      tft.drawWideLine(295, y + 12, 300, y + 17, 1.8f, ui::C_MUTED, ui::C_CARD);
      tft.drawWideLine(300, y + 17, 295, y + 22, 1.8f, ui::C_MUTED, ui::C_CARD);
    }
  }
  tft.setTextFont(1);
}

void drawNewsDetailScreen(int newsIndex) {
  if (newsIndex < 0 || newsIndex >= idleData.newsCount) { newsDetailIdx = -1; drawNewsScreen(); return; }
  newsDetailIdx   = newsIndex;
  newsDetailTitle = idleData.news[newsIndex];
  const NewsMeta& m = idleData.newsMeta[newsIndex];
  const int total = idleData.newsCount < 5 ? idleData.newsCount : 5;

  tft.fillScreen(C_BG);
  const int CX = 10, CY = 6, CW = 300, CH = 198, PAD = 12;
  const int IX = CX + PAD, IW = CW - 2 * PAD;
  tft.fillRoundRect(CX, CY, CW, CH, 12, ui::C_CARD);

  char badge[24];
  snprintf(badge, sizeof(badge), "ACTUALITE %d/%d", newsIndex + 1, total);
  tft.setTextFont(2);
  const int bw = tft.textWidth(badge) + 16;
  tft.fillRoundRect(IX, CY + 10, bw, 20, 10, C_ACCENT);
  tft.setTextColor(C_TEXT, C_ACCENT);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(badge, IX + bw / 2, CY + 20);
  int rx = IX + IW;
  tft.setTextColor(C_DIM, ui::C_CARD);
  tft.setTextDatum(MR_DATUM);
  if (m.when.length()) { tft.drawString(m.when, rx, CY + 20); rx -= tft.textWidth(m.when) + 10; }
  if (m.src.length()) {
    const int room = rx - (IX + bw + 10);
    if (room > 30) { char sb[40]; ellipsizeOn(tft, m.src.c_str(), sb, sizeof(sb), room); tft.drawString(sb, rx, CY + 20); }
  }

  const bool  hasDesc = m.desc.length() > 0;
  const char* title   = idleData.news[newsIndex].c_str();
  txt::Line   tl[8];
  bool        tTrunc  = false;
  int         tLineH  = 28;
  tft.setFreeFont(&FreeSansBold12pt7b);
  int tn = txt::wrap(title, IW, tftWidthN, tl, hasDesc ? 2 : 5, &tTrunc);
  if (tTrunc) {
    tft.setFreeFont(&FreeSansBold9pt7b);
    tLineH = 22;
    tn = txt::wrap(title, IW, tftWidthN, tl, hasDesc ? 3 : 7, &tTrunc);
  }
  const int TY = CY + 40;
  tft.setTextColor(C_TEXT, ui::C_CARD);
  tft.setTextDatum(ML_DATUM);
  int y = TY + tLineH / 2;
  for (int i = 0; i < tn; i++, y += tLineH) {
    if (i == tn - 1 && tTrunc) {
      char eb[160]; ellipsizeOn(tft, title + tl[i].start, eb, sizeof(eb), IW); tft.drawString(eb, IX, y);
    } else {
      drawSlice(title, tl[i], IX, y);
    }
  }

  const int sepY = TY + tn * tLineH + 2, bottom = CY + CH - 8;
  if (hasDesc) {
    tft.drawFastHLine(IX, sepY, IW, C_TRACK);
    tft.setFreeFont(&FreeSans9pt7b);
    const int dLineH = 21, descTop = sepY + 6;
    int dMax = (bottom - descTop) / dLineH;
    if (dMax > 6) dMax = 6;
    if (dMax >= 1) {
      txt::Line dl[6];
      bool dTrunc = false;
      const char* desc = m.desc.c_str();
      int dn = txt::wrap(desc, IW, tftWidthN, dl, dMax, &dTrunc);
      tft.setTextColor(C_DIM, ui::C_CARD);
      y = descTop + dLineH / 2;
      for (int i = 0; i < dn; i++, y += dLineH) {
        if (i == dn - 1 && dTrunc) { char eb[160]; ellipsizeOn(tft, desc + dl[i].start, eb, sizeof(eb), IW); tft.drawString(eb, IX, y); }
        else drawSlice(desc, dl[i], IX, y);
      }
    }
  }

  tft.fillRoundRect(110, 210, 100, 24, 10, C_BTN);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT, C_BTN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("< Retour", 160, 222);
  tft.setTextFont(1);
}

static const int MN_X = 10, MN_W = 300;
static const int MN_BRI_Y = 38, MN_BRI_H = 54;
static const int MN_SLD_X0 = 26, MN_SLD_X1 = 294, MN_SLD_Y = 77;
static const int MN_ROW_Y = 98, MN_ROW_H = 26, MN_ROWS = 4;
static const int MN_BTN_Y = 208, MN_BTN_H = 28, MN_BTN_W = 145;
static bool     menuRebootArmed = false;
static uint32_t menuRebootMs    = 0;
static bool     menuDrag        = false;
static uint32_t menuHdrMs       = 0;
static uint8_t  menuPage        = 0;
static bool     menuDirty       = false;
static const char* const LED_NAMES[4] = { "Eteinte", "Accent", "Pulsation", "Basses" };

static void menuHeader() {
  menuHdrMs = millis();
  tft.fillRect(0, 0, SCR_W, 34, C_BG);
  tft.drawWideLine(22, 10, 14, 17, 2.5f, C_TEXT, C_BG);
  tft.drawWideLine(14, 17, 22, 24, 2.5f, C_TEXT, C_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(menuPage == 1 ? "Mode nuit" : "Reglages", 34, 17);
  tft.setTextFont(1);
  if (menuPage == 1) return;
  const int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -100;
  const int lvl  = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -90 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    const int h = 4 + i * 3;
    tft.fillRect(292 + i * 5, 27 - h, 3, h, i < lvl ? C_TEXT : ui::C_TRACK);
  }
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(WiFi.localIP().toString(), 282, 8);
  tft.setTextColor(RGB565(90, 90, 98), C_BG);
  tft.drawString("v" FW_VERSION, 282, 20);
}

static void menuBriValue() {
  tft.fillRect(190, 43, 108, 20, ui::C_CARD);
  char b[16];
  if (cfg.autoBri) snprintf(b, sizeof(b), "max %d%%", (int)cfg.briPct);
  else             snprintf(b, sizeof(b), "%d%%", (int)cfg.briPct);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(MR_DATUM); tft.setTextColor(ui::C_MUTED, ui::C_CARD);
  tft.drawString(b, 296, 53);
  tft.setTextFont(1);
}

static void menuSlider() {
  const int span = MN_SLD_X1 - MN_SLD_X0;
  const int fx = MN_SLD_X0 + span * ((int)cfg.briPct - 5) / 95;
  const int fw = fx - MN_SLD_X0 + 1;
  tft.fillRect(MN_X + 6, MN_SLD_Y - 10, MN_W - 12, 20, ui::C_CARD);
  tft.fillRoundRect(MN_SLD_X0, MN_SLD_Y - 3, span, 6, 3, ui::C_TRACK);
  tft.fillRoundRect(MN_SLD_X0, MN_SLD_Y - 3, fw, 6, fw < 6 ? fw / 2 : 3, C_ACCENT);
  tft.fillSmoothCircle(fx, MN_SLD_Y, 8, C_TEXT, ui::C_CARD);
}

static void menuBrightnessCard() {
  tft.fillRoundRect(MN_X, MN_BRI_Y, MN_W, MN_BRI_H, ui::R_CARD, ui::C_CARD);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, ui::C_CARD);
  tft.drawString("Luminosite", 24, 53);
  tft.setTextFont(1);
  menuBriValue();
  menuSlider();
}

static void menuToggle(int rightX, int cy, bool on) {
  const int w = 40, h = 22, x = rightX - w, y = cy - h / 2;
  const uint16_t bg = on ? C_ACCENT : ui::C_TRACK;
  tft.fillRoundRect(x, y, w, h, h / 2, bg);
  tft.fillSmoothCircle(on ? x + w - h / 2 : x + h / 2, cy, h / 2 - 3, C_TEXT, bg);
}

static void menuRow(int i, bool full) {
  const int y = MN_ROW_Y + i * MN_ROW_H, cy = y + MN_ROW_H / 2;
  tft.fillRect(150, y + 2, 154, MN_ROW_H - 4, ui::C_CARD);
  tft.setFreeFont(&FreeSans9pt7b);
  if (full) {
    if (i > 0) tft.drawFastHLine(24, y, MN_W - 28, ui::C_TRACK);
    tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, ui::C_CARD);
    tft.drawString(i == 0 ? "Mode nuit" : i == 1 ? "Luminosite auto" : i == 2 ? "LED RGB" : "Ecran", 24, cy);
  }
  char b[24];
  if (i == 0) {
    snprintf(b, sizeof(b), "%02dh-%02dh", (int)cfg.nightStart, (int)cfg.nightEnd);
    tft.setTextDatum(MR_DATUM); tft.setTextColor(C_ACCENT, ui::C_CARD);
    tft.drawString(b, 242, cy);
    menuToggle(296, cy, cfg.nightOn);
  } else if (i == 1) {
    menuToggle(296, cy, cfg.autoBri);
  } else {
    if (i == 2) {
      const uint8_t m = cfg.ledMode < 4 ? cfg.ledMode : 0;
      tft.setTextDatum(MR_DATUM); tft.setTextColor(m ? C_ACCENT : ui::C_MUTED, ui::C_CARD);
      tft.drawString(LED_NAMES[m], 280, cy);
    } else {
      tft.setTextDatum(MR_DATUM); tft.setTextColor(ui::C_MUTED, ui::C_CARD);
      tft.drawString("Eteindre", 280, cy);
    }
    tft.drawWideLine(288, cy - 5, 294, cy, 2.0f, ui::C_MUTED, ui::C_CARD);
    tft.drawWideLine(294, cy, 288, cy + 5, 2.0f, ui::C_MUTED, ui::C_CARD);
  }
  tft.setTextFont(1);
}

static void menuButtons() {
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.fillRoundRect(MN_X, MN_BTN_Y, MN_BTN_W, MN_BTN_H, 10, ui::C_CARD2);
  tft.setTextColor(C_TEXT, ui::C_CARD2);
  tft.drawString("Diagnostic", MN_X + MN_BTN_W / 2, MN_BTN_Y + MN_BTN_H / 2);
  const uint16_t rb = menuRebootArmed ? RGB565(200, 40, 40) : RGB565(72, 24, 26);
  const int rx = MN_X + MN_W - MN_BTN_W;
  tft.fillRoundRect(rx, MN_BTN_Y, MN_BTN_W, MN_BTN_H, 10, rb);
  tft.setTextColor(C_TEXT, rb);
  tft.drawString(menuRebootArmed ? "Confirmer ?" : "Redemarrer", rx + MN_BTN_W / 2, MN_BTN_Y + MN_BTN_H / 2);
  tft.setTextFont(1);
}

static const int NT_Y = 40, NT_ROW_H = 44;

static void nightRow(int row, bool full) {
  const int y = NT_Y + row * NT_ROW_H, cy = y + NT_ROW_H / 2;
  tft.fillRect(120, y + 2, 184, NT_ROW_H - 4, ui::C_CARD);
  tft.setFreeFont(&FreeSans9pt7b);
  if (full) {
    if (row > 0) tft.drawFastHLine(24, y, MN_W - 28, ui::C_TRACK);
    tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, ui::C_CARD);
    tft.drawString(row == 0 ? "Activer" : row == 1 ? "Debut" : row == 2 ? "Fin" : "Luminosite", 24, cy);
  }
  if (row == 0) { menuToggle(296, cy, cfg.nightOn); tft.setTextFont(1); return; }
  char b[12];
  if (row == 1)      snprintf(b, sizeof(b), "%02dh", (int)cfg.nightStart);
  else if (row == 2) snprintf(b, sizeof(b), "%02dh", (int)cfg.nightEnd);
  else               snprintf(b, sizeof(b), "%d%%", (int)cfg.nightBri);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(C_TEXT, ui::C_CARD);
  tft.drawString(b, 232, cy);
  for (int k = 0; k < 2; k++) {
    const int bx = k ? 278 : 190;
    tft.fillSmoothCircle(bx, cy, 14, ui::C_CARD2, ui::C_CARD);
    tft.drawWideLine(bx - 6, cy, bx + 6, cy, 2.5f, C_TEXT, ui::C_CARD2);
    if (k) tft.drawWideLine(bx, cy - 6, bx, cy + 6, 2.5f, C_TEXT, ui::C_CARD2);
  }
  tft.setTextFont(1);
}

static void drawNightPage() {
  tft.fillScreen(C_BG);
  menuHeader();
  tft.fillRoundRect(MN_X, NT_Y, MN_W, NT_ROW_H * 4, ui::R_CARD, ui::C_CARD);
  for (int i = 0; i < 4; i++) nightRow(i, true);
  tft.setTextFont(1); tft.setTextDatum(TC_DATUM); tft.setTextColor(RGB565(90, 90, 98), C_BG);
  tft.drawString("Pendant la plage choisie, l'ecran passe a la luminosite de nuit", SCR_W / 2, 224);
}

static void nightStep(int row, int dir) {
  if (row == 1)      cfg.nightStart = (uint8_t)((cfg.nightStart + 24 + dir) % 24);
  else if (row == 2) cfg.nightEnd   = (uint8_t)((cfg.nightEnd   + 24 + dir) % 24);
  else if (row == 3) cfg.nightBri   = (uint8_t)constrain((int)cfg.nightBri + dir, 1, 60);
  else return;
  menuDirty = true;
  autoDimReset = true;
  nightRow(row, false);
}

static int      nightHoldRow = -1, nightHoldDir = 0;
static uint32_t nightHoldMs = 0, nightRepMs = 0;

static bool nightHit(int x, int y, int* row, int* dir) {
  if (y < NT_Y || y >= NT_Y + NT_ROW_H * 4) return false;
  *row = (y - NT_Y) / NT_ROW_H;
  if (*row == 0) { *dir = 0; return true; }
  if (x >= 166 && x < 214) { *dir = -1; return true; }
  if (x >= 254 && x < 306) { *dir = +1; return true; }
  return false;
}

static void nightTouchDown(int x, int y) {
  if (x < 80 && y < 38) { menuPage = 0; drawMenuScreen(); return; }
  int row, dir;
  if (!nightHit(x, y, &row, &dir)) return;
  if (row == 0) { cfg.nightOn = !cfg.nightOn; saveSettings(); autoDimReset = true; nightRow(0, false); return; }
  nightStep(row, dir);
  nightHoldRow = row; nightHoldDir = dir; nightHoldMs = nightRepMs = millis();
}

void drawMenuScreen() {
  menuRebootArmed = false; menuDrag = false; menuDirty = false; menuPage = 0; nightHoldRow = -1;
  tft.fillScreen(C_BG);
  menuHeader();
  menuBrightnessCard();
  tft.fillRoundRect(MN_X, MN_ROW_Y, MN_W, MN_ROW_H * MN_ROWS, ui::R_CARD, ui::C_CARD);
  for (int i = 0; i < MN_ROWS; i++) menuRow(i, true);
  menuButtons();
}

static void menuSetBrightnessFromX(int x, bool force) {
  const int v = constrain(map(x, MN_SLD_X0, MN_SLD_X1, 5, 100), 5, 100);
  static uint32_t last = 0;
  if (v == (int)cfg.briPct || (!force && millis() - last < 40)) return;
  last = millis();
  cfg.briPct = (uint8_t)v;
  applyBrightness();
  menuBriValue(); menuSlider();
}

void menuTouchDown(int x, int y) {
  if (menuPage == 1) { nightTouchDown(x, y); return; }
  if (x < 80 && y < 38) { menuRebootArmed = false; cur = SCR_WAIT; idleDrawn = false; return; }
  if (y >= 62 && y <= 94) { menuDrag = true; menuSetBrightnessFromX(x, true); return; }
  if (y >= MN_ROW_Y && y < MN_ROW_Y + MN_ROW_H * MN_ROWS) {
    const int i = (y - MN_ROW_Y) / MN_ROW_H;
    if (i == 0) {
      if (x >= 248) { cfg.nightOn = !cfg.nightOn; saveSettings(); autoDimReset = true; menuRow(0, false); }
      else          { menuPage = 1; drawNightPage(); }
      return;
    }
    if (i == 1)      { cfg.autoBri = !cfg.autoBri; saveSettings(); applyBrightness(); menuBriValue(); }
    else if (i == 2) { cfg.ledMode = (uint8_t)((cfg.ledMode + 1) % 4); saveSettings(); }
    else             { screenOff = true; blWrite(0); return; }
    menuRow(i, false);
    return;
  }
  if (y >= MN_BTN_Y - 4) {
    if (x < SCR_W / 2) { menuRebootArmed = false; cur = SCR_SYS; drawSystemMemoryScreen(); return; }
    if (!menuRebootArmed) { menuRebootArmed = true; menuRebootMs = millis(); menuButtons(); return; }
    ESP.restart();
  }
}

void menuTouchDrag(int x, int y) {
  if (menuPage == 1) {
    if (nightHoldRow < 0 || millis() - nightHoldMs < 500 || millis() - nightRepMs < 110) return;
    int row, dir;
    if (nightHit(x, y, &row, &dir) && row == nightHoldRow && dir == nightHoldDir) { nightRepMs = millis(); nightStep(row, dir); }
    return;
  }
  if (menuDrag) menuSetBrightnessFromX(x, false);
}

void menuTouchUp(int x) {
  nightHoldRow = -1;
  if (menuDirty) { menuDirty = false; saveSettings(); }
  if (!menuDrag) return;
  menuDrag = false;
  menuSetBrightnessFromX(x, true);
  saveSettings();
}

void tickMenuScreen() {
  if (cur != SCR_MENU || menuDrag || menuPage != 0) return;
  if (menuRebootArmed && millis() - menuRebootMs > 4000) { menuRebootArmed = false; menuButtons(); }
  if (millis() - menuHdrMs > 10000UL) menuHeader();
}

static uint8_t  sysPage = 0;
static uint32_t sysLastMs = 0;
static int      sysGraphHead = -1;
static bool     spiffsTried = false, spiffsOk = false;
static const uint16_t C_OK  = RGB565(48, 209, 88);
static const uint16_t C_BAD = RGB565(255, 69, 58);
static const int SY_TAB_Y = 204, SY_TAB_H = 30;

static uint16_t levelColor(uint32_t v, uint32_t warn, uint32_t good) { return v >= good ? C_OK : (v >= warn ? ui::C_AMBER : C_BAD); }

static void fmtBytes(char* o, size_t n, uint32_t b) {
  if (b >= 1048576UL) snprintf(o, n, "%u.%02u Mo", (unsigned)(b / 1048576UL), (unsigned)((b % 1048576UL) * 100 / 1048576UL));
  else                snprintf(o, n, "%u.%u Ko", (unsigned)(b / 1024), (unsigned)((b % 1024) * 10 / 1024));
}

static void sysBar(int x, int y, int w, int h, uint32_t val, uint32_t tot, uint16_t col) {
  tft.fillRoundRect(x, y, w, h, h / 2, C_TRACK);
  if (tot == 0) return;
  if (val > tot) val = tot;
  int fw = (int)(((uint64_t)w * val) / tot);
  if (val > 0 && fw < h) fw = h;
  if (fw > 0) tft.fillRoundRect(x, y, fw, h, h / 2, col);
}

static void sysRow(int y, const char* label, const char* value, uint16_t col, uint16_t bg) {
  tft.setTextFont(2);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(C_DIM, bg);
  tft.drawString(label, 20, y);
  tft.fillRect(170, y, 130, 16, bg);
  tft.setTextDatum(TR_DATUM); tft.setTextColor(col, bg);
  tft.drawString(value, 300, y);
}

static void sysVerdict(uint16_t* col, const char** txt) {
  const diag::Heap h = diag::snap();
  if (diag::leakSuspected())              { *col = C_BAD;        *txt = "Fuite possible"; }
  else if (h.maxBlk < diag::WARN_MAXBLK)  { *col = ui::C_AMBER;  *txt = "Fragmente"; }
  else                                    { *col = C_OK;         *txt = "Stable"; }
}

static void sysHeader(bool full) {
  if (full) {
    tft.fillRect(0, 0, SCR_W, 34, C_BG);
    tft.drawWideLine(22, 10, 14, 17, 2.5f, C_TEXT, C_BG);
    tft.drawWideLine(14, 17, 22, 24, 2.5f, C_TEXT, C_BG);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextDatum(ML_DATUM); tft.setTextColor(C_TEXT, C_BG);
    tft.drawString("Systeme", 34, 17);
  }
  tft.fillRect(150, 4, 166, 26, C_BG);
  uint16_t col; const char* txt;
  sysVerdict(&col, &txt);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(MR_DATUM); tft.setTextColor(ui::C_MUTED, C_BG);
  tft.drawString(txt, 306, 17);
  const int tw = tft.textWidth(txt);
  tft.fillSmoothCircle(306 - tw - 11, 17, 4, col, C_BG);
  tft.setTextFont(1);
}

static void sysTabs() {
  static const char* const NAMES[3] = { "Resume", "Memoire", "Stockage" };
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < 3; i++) {
    const int x = 10 + i * 102;
    const uint16_t bg = (i == sysPage) ? C_ACCENT : ui::C_CARD;
    tft.fillRoundRect(x, SY_TAB_Y, 96, SY_TAB_H, 10, bg);
    tft.setTextColor(i == sysPage ? C_TEXT : ui::C_MUTED, bg);
    tft.drawString(NAMES[i], x + 48, SY_TAB_Y + SY_TAB_H / 2);
  }
  tft.setTextFont(1);
}

static const int SY_TX0 = 10, SY_TX1 = 165, SY_TW = 145, SY_TH = 48, SY_TY0 = 40, SY_TPITCH = 54;

static void sysTile(int i, const char* label, const char* value, uint16_t col, int barPct, bool full) {
  const int x = (i & 1) ? SY_TX1 : SY_TX0, y = SY_TY0 + (i >> 1) * SY_TPITCH;
  if (full) {
    tft.fillRoundRect(x, y, SY_TW, SY_TH, 10, ui::C_CARD);
    tft.setTextFont(1); tft.setTextDatum(TL_DATUM); tft.setTextColor(ui::C_MUTED, ui::C_CARD);
    tft.drawString(label, x + 10, y + 7);
  }
  tft.fillRect(x + 6, y + 17, SY_TW - 12, SY_TH - 19, ui::C_CARD);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(col, ui::C_CARD);
  tft.drawString(value, x + 10, y + 28);
  tft.setTextFont(1);
  if (barPct >= 0) sysBar(x + 10, y + SY_TH - 9, SY_TW - 20, 4, (uint32_t)barPct, 100, col);
}

static void sysDrawSummary(bool full) {
  const diag::Heap h = diag::snap();
  diag::note(h);
  char v[24];
  const uint32_t tot = ESP.getHeapSize();
  fmtBytes(v, sizeof(v), h.freeB);
  sysTile(0, "Memoire libre", v, levelColor(h.freeB, 35000, 60000), tot ? (int)((uint64_t)h.freeB * 100 / tot) : 0, full);
  fmtBytes(v, sizeof(v), h.maxBlk);
  sysTile(1, "Plus grand bloc", v, levelColor(h.maxBlk, diag::WARN_MAXBLK, 40000), h.freeB ? (int)((uint64_t)h.maxBlk * 100 / h.freeB) : 0, full);
  const int frag = diag::fragPct(h);
  snprintf(v, sizeof(v), "%d %%", frag);
  sysTile(2, "Fragmentation", v, levelColor((uint32_t)(100 - frag), 50, 75), frag, full);
  const float t = temperatureRead();
  snprintf(v, sizeof(v), "%d C", (int)lroundf(t));
  sysTile(3, "Temperature de la puce", v, t < 70.0f ? C_OK : (t < 80.0f ? ui::C_AMBER : C_BAD), -1, full);
  snprintf(v, sizeof(v), "%u Hz", (unsigned)loopStat.hz);
  sysTile(4, "Boucle principale", v, loopStat.slowLast ? ui::C_AMBER : C_TEXT, -1, full);
  char up[24]; diag::fmtUptime(up, sizeof(up), millis() / 1000);
  sysTile(5, "En marche depuis", up, C_TEXT, -1, full);
}

static void sysDrawGraph() {
  const int GX = 22, GY = 64, GW = 276, GH = 76;
  tft.fillRect(GX - 2, GY - 2, GW + 4, GH + 4, ui::C_CARD);
  diag::History& H = diag::hist();
  sysGraphHead = H.head;
  if (H.n < 2) {
    tft.setTextFont(1); tft.setTextDatum(MC_DATUM); tft.setTextColor(C_DIM, ui::C_CARD);
    tft.drawString("collecte en cours...", GX + GW / 2, GY + GH / 2);
    return;
  }
  uint32_t mn = 0xFFFFFFFFu, mx = 0;
  for (int i = 0; i < H.n; i++) {
    uint32_t v = diag::histFree(i), w = diag::histMaxBlk(i);
    if (v < mn) mn = v;
    if (w < mn) mn = w;
    if (v > mx) mx = v;
  }
  if (mx < mn + 2048) mx = mn + 2048;
  for (int pass = 0; pass < 2; pass++) {
    const uint16_t col = pass ? ui::C_AMBER : ui::C_BLUE;
    int px = 0, py = 0;
    for (int i = 0; i < H.n; i++) {
      uint32_t v = pass ? diag::histMaxBlk(i) : diag::histFree(i);
      if (v < mn) v = mn;
      if (v > mx) v = mx;
      int x = GX + (int)(((long)i * (GW - 1)) / (H.n - 1));
      int y = GY + GH - 1 - (int)(((uint64_t)(v - mn) * (GH - 1)) / (mx - mn));
      if (i) tft.drawLine(px, py, x, y, col);
      px = x; py = y;
    }
  }
}

static void sysDrawMemory(bool full) {
  const diag::Heap h = diag::snap();
  diag::note(h);
  if (full) {
    tft.fillRoundRect(10, 40, 300, 108, 12, ui::C_CARD);
    tft.fillRoundRect(10, 152, 300, 48, 12, ui::C_CARD);
    tft.setTextFont(1); tft.setTextDatum(TL_DATUM); tft.setTextColor(C_DIM, ui::C_CARD);
    tft.fillSmoothCircle(24, 51, 3, ui::C_BLUE, ui::C_CARD);  tft.drawString("libre", 31, 47);
    tft.fillSmoothCircle(72, 51, 3, ui::C_AMBER, ui::C_CARD); tft.drawString("bloc max", 79, 47);
    tft.setTextDatum(TR_DATUM); tft.drawString("4 min", 298, 47);
  }
  if (full || diag::hist().head != sysGraphHead) sysDrawGraph();
  char a[32];
  fmtBytes(a, sizeof(a), h.minFree);
  sysRow(157, "Min. libre (boot)", a, levelColor(h.minFree, 20000, 40000), ui::C_CARD);
  const uint32_t worst = (diag::hist().worstBlk == 0xFFFFFFFFu) ? h.maxBlk : diag::hist().worstBlk;
  fmtBytes(a, sizeof(a), worst);
  sysRow(177, "Pire bloc contigu", a, levelColor(worst, diag::WARN_MAXBLK, 40000), ui::C_CARD);
  tft.setTextFont(1);
}

struct SdEntry { char name[22]; uint32_t size; };
static SdEntry  sdLast[7];
static uint32_t sdScanCount = 0, sdCacheBytes = 0;

static void sdScanCache() {
  sdScanCount = 0; sdCacheBytes = 0;
  if (!sdOk) return;
  sdBus();
  const char* dirs[2] = { SD_DIR_S, SD_DIR_B };
  for (int k = 0; k < 2; k++) {
    File d = SD.open(dirs[k]);
    if (!d) continue;
    File f = d.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        const char* nm = f.name();
        const char* sl = strrchr(nm, '/');
        SdEntry& e = sdLast[sdScanCount % 7];
        snprintf(e.name, sizeof(e.name), "%c/%s", k ? 'b' : 's', sl ? sl + 1 : nm);
        e.size = (uint32_t)f.size();
        sdCacheBytes += e.size;
        sdScanCount++;
      }
      f.close();
      f = d.openNextFile();
    }
    d.close();
  }
  touchBus();
}

static void sysBufLine(int y, const char* label, uint32_t used, uint32_t cap, uint16_t col) {
  char v[32];
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(C_DIM, ui::C_CARD);
  tft.drawString(label, 20, y);
  snprintf(v, sizeof(v), "%u / %u o", (unsigned)used, (unsigned)cap);
  tft.setTextDatum(TR_DATUM); tft.setTextColor(col, ui::C_CARD);
  tft.drawString(v, 300, y);
}

static void sysDrawStorage() {
  tft.setTextFont(2); tft.setTextDatum(MC_DATUM); tft.setTextColor(C_DIM, C_BG);
  tft.drawString("Lecture...", SCR_W / 2, 110);
  sdScanCache();
  if (!spiffsTried) { spiffsTried = true; spiffsOk = SPIFFS.begin(false); }
  tft.fillRect(0, 36, SCR_W, SY_TAB_Y - 36, C_BG);

  char a[32], b[32], v[72];
  tft.fillRoundRect(10, 40, 300, 62, 12, ui::C_CARD);
  const uint32_t sk = ESP.getSketchSize(), skTot = sk + ESP.getFreeSketchSpace();
  fmtBytes(a, sizeof(a), sk); fmtBytes(b, sizeof(b), skTot);
  snprintf(v, sizeof(v), "%s / %s", a, b);
  sysRow(44, "Firmware (slot OTA)", v, levelColor(skTot - sk, 100000, 300000), ui::C_CARD);
  sysBar(20, 62, 280, 5, sk, skTot, C_ACCENT);
  if (spiffsOk) {
    fmtBytes(a, sizeof(a), (uint32_t)SPIFFS.usedBytes()); fmtBytes(b, sizeof(b), (uint32_t)SPIFFS.totalBytes());
    snprintf(v, sizeof(v), "%s / %s", a, b);
    sysRow(70, "SPIFFS", v, C_TEXT, ui::C_CARD);
    sysBar(20, 88, 280, 5, SPIFFS.usedBytes(), SPIFFS.totalBytes(), ui::C_BLUE);
  } else {
    sysRow(70, "SPIFFS", "non monte (inutilise)", C_DIM, ui::C_CARD);
  }

  tft.fillRoundRect(10, 106, 300, 44, 12, ui::C_CARD);
  if (!cfg.sdCache) {
    sysRow(110, "Carte SD", "cache desactive", C_DIM, ui::C_CARD);
  } else if (!sdOk) {
    sysRow(110, "Carte SD", "absente", C_BAD, ui::C_CARD);
  } else {
    snprintf(v, sizeof(v), "%u Mo", (unsigned)sdSizeMB);
    sysRow(110, "Carte SD", v, C_TEXT, ui::C_CARD);
    fmtBytes(a, sizeof(a), sdCacheBytes);
    snprintf(v, sizeof(v), "cache : %u fichiers, %s (max %u pistes)", (unsigned)sdScanCount, a, (unsigned)cfg.sdMax);
    tft.setTextFont(1); tft.setTextDatum(TL_DATUM); tft.setTextColor(C_DIM, ui::C_CARD);
    tft.drawString(v, 20, 130);
  }

  tft.fillRoundRect(10, 154, 300, 46, 12, ui::C_CARD);
  sysBufLine(160, coverFails ? "Pochette (echecs !)" : "Pochette", (uint32_t)coverLen, COVER_MAX_BYTES, coverFails ? C_BAD : C_TEXT);
  sysBufLine(172, "Pochette plein ecran", (uint32_t)bigLen, BIG_MAX_BYTES, C_TEXT);
  sysBufLine(184, "Logo de l'appli", (uint32_t)iconLen, ICON_MAX_BYTES, C_TEXT);
  tft.setTextFont(1);
}

static void diagDumpSerial() {
  const diag::Heap h = diag::snap();
  char up[24]; diag::fmtUptime(up, sizeof(up), millis() / 1000);
  Serial.printf("[diag] up=%s heap: free=%u max=%u min=%u total=%u frag=%d%% worstBlk=%u leak=%d\n", up,
                (unsigned)h.freeB, (unsigned)h.maxBlk, (unsigned)h.minFree, (unsigned)ESP.getHeapSize(), diag::fragPct(h),
                (unsigned)diag::hist().worstBlk, (int)diag::leakSuspected());
  Serial.printf("[diag] flash: sketch=%u free=%u | cover=%u/%u big=%u/%u icon=%u/%u | coverFails=%u\n",
                (unsigned)ESP.getSketchSize(), (unsigned)ESP.getFreeSketchSpace(), (unsigned)coverLen, (unsigned)COVER_MAX_BYTES,
                (unsigned)bigLen, (unsigned)BIG_MAX_BYTES, (unsigned)iconLen, (unsigned)ICON_MAX_BYTES, (unsigned)coverFails);
}

static void sysShowPage(uint8_t p) {
  sysPage = p;
  sysLastMs = millis();
  sysGraphHead = -1;
  tft.fillRect(0, 36, SCR_W, SY_TAB_Y - 36, C_BG);
  if (p == 0)      sysDrawSummary(true);
  else if (p == 1) sysDrawMemory(true);
  else             sysDrawStorage();
  sysTabs();
}

void drawSystemMemoryScreen() {
  diagDumpSerial();
  tft.fillScreen(C_BG);
  sysHeader(true);
  sysShowPage(0);
}

void sysTouchDown(int x, int y) {
  if (x < 80 && y < 38) { cur = SCR_MENU; drawMenuScreen(); return; }
  if (y >= SY_TAB_Y - 6) {
    const uint8_t i = x < 108 ? 0 : (x < 214 ? 1 : 2);
    if (i != sysPage) sysShowPage(i);
  }
}

static void tickSysScreen() {
  if (cur != SCR_SYS || sysPage == 2) return;
  if (millis() - sysLastMs < 1000) return;
  sysLastMs = millis();
  if (sysPage == 0) sysDrawSummary(false); else sysDrawMemory(false);
  sysHeader(false);
}

void handleDiag() {
  const diag::Heap h = diag::snap();
  JsonDocument d;
  d["version"] = FW_VERSION;
  d["uptime"]  = millis() / 1000;
  d["heap"]["free"]       = h.freeB;
  d["heap"]["maxBlock"]   = h.maxBlk;
  d["heap"]["minFree"]    = h.minFree;
  d["heap"]["total"]      = ESP.getHeapSize();
  d["heap"]["fragPct"]    = diag::fragPct(h);
  d["heap"]["worstBlock"] = diag::hist().worstBlk;
  d["heap"]["leakSuspected"] = diag::leakSuspected();
  d["flash"]["sketch"]     = ESP.getSketchSize();
  d["flash"]["freeSketch"] = ESP.getFreeSketchSpace();
  d["flash"]["chip"]       = ESP.getFlashChipSize();
  d["buffers"]["cover"] = coverLen;
  d["buffers"]["big"]   = bigLen;
  d["buffers"]["icon"]  = iconLen;
  d["cover"]["fails"]   = coverFails;
  d["cover"]["failId"]  = coverFailId;
  d["sd"]["enabled"] = cfg.sdCache;
  d["sd"]["ok"]      = sdOk;
  d["sd"]["sizeMB"]  = sdSizeMB;
  d["sd"]["files"]   = sdCount;
  JsonArray hf = d["history"]["free"].to<JsonArray>();
  JsonArray hb = d["history"]["maxBlock"].to<JsonArray>();
  for (int i = 0; i < diag::hist().n; i++) { hf.add(diag::histFree(i)); hb.add(diag::histMaxBlk(i)); }
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

static uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t  ) {
  const int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  const int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar * (255 - t) + br * t) / 255) << 11) |
                    (((ag * (255 - t) + bg * t) / 255) << 5)  |
                     ((ab * (255 - t) + bb * t) / 255));
}

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
  const uint16_t C_COLD = RGB565(100, 210, 255);
  tft.fillScreen(C_BG);

  time_t t = time(nullptr) + (time_t)day * 86400;
  struct tm tm; localtime_r(&t, &tm);
  char buf[48];

  const int CX = 20, CY = 6, CW = 280, CH = 200, MID = CX + CW / 2;
  tft.fillRoundRect(CX, CY, CW, CH, 10, ui::C_CARD);

  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextColor(C_TEXT, ui::C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(dayNames[tm.tm_wday], MID, CY + 26);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_DIM, ui::C_CARD);
  snprintf(buf, sizeof(buf), day == 0 ? "Aujourd'hui - %d %s" : "%d %s", tm.tm_mday, monthNames[tm.tm_mon]);
  tft.drawString(buf, MID, CY + 52);

  ui::drawWxIcon(tft, MID, CY + 96, ui::wmoToIcon(f.code), 1.3f, ui::C_CARD);
  tft.setTextColor(C_DIM, ui::C_CARD);
  tft.drawString(wmoDesc(f.code).c_str(), MID, CY + 138);

  drawLabelTemp(CX + CW / 4,     CY + 162, "Max", (int)lroundf(f.tMax), ui::C_AMBER);
  drawLabelTemp(CX + CW * 3 / 4, CY + 162, "Min", (int)lroundf(f.tMin), C_COLD);

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

  tft.fillRoundRect(110, 212, 100, 24, 10, C_BTN);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT, C_BTN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("< Retour", 160, 224);
  tft.setTextFont(1);
}

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
  const float  oLat = idleCfg.lat, oLon = idleCfg.lon;
  const String oRss = idleCfg.rssUrl, oTz = idleCfg.tz;
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
  if (fabsf(idleCfg.lat - oLat) > 0.0001f || fabsf(idleCfg.lon - oLon) > 0.0001f || idleCfg.rssUrl != oRss || idleCfg.tz != oTz) {
    lastWxFetch  = 0;
    lastRssFetch = 0;
    firstFetchDone = false;
  }
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
  h += "<label>Jeton de mise a jour (optionnel)</label><input name=tok autocomplete=off placeholder='a recopier dans l appli PC'>";
  h += "<button>Enregistrer et redemarrer</button></form><small>Seuls les reseaux 2,4 GHz sont pris en charge.</small></body></html>";
  server.send(200, "text/html; charset=utf-8", h);
}

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
  String tok = server.arg("tok"); tok.trim();
  if (tok.length() >= 4 && tok.length() <= 64) prefs.putString("otk", tok);
  prefs.end();
  server.send(200, "text/html; charset=utf-8", String(PAGE_HEAD) +
    "<h1>Enregistre</h1><p>La carte redemarre et se connecte a &laquo; " + htmlEsc(ssid) + " &raquo;.</p></body></html>");
  delay(800);
  ESP.restart();
}

void handleReboot() {
  if (!tokenOk(server.arg("token"))) { server.send(403, "text/plain", "jeton refuse"); return; }
  server.send(200, "text/html; charset=utf-8", String(PAGE_HEAD) + "<p>Redemarrage...</p></body></html>");
  delay(400);
  ESP.restart();
}

void handleWifiReset() {
  if (!tokenOk(server.arg("token"))) { server.send(403, "text/plain", "jeton refuse"); return; }
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.putString("otk", otaToken);
  prefs.end();
  server.send(200, "text/html; charset=utf-8", String(PAGE_HEAD) + "<p>Wi-Fi oublie, la carte redemarre.</p></body></html>");
  delay(400);
  ESP.restart();
}

void handleNotFound() {
  if (portalActive) {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "not found");
}

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
    otaDenied = !tokenOk(server.arg("token"));
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

bool netWasUp = false, serverStarted = false;

static bool is24GHz(int32_t ch) { return ch >= 1 && ch <= 13; }

static int pick24GHz(int n, uint8_t* bssidOut) {
  int bestRssi = -9999, bestCh = -1;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != wifiSsid) continue;
    int ch = WiFi.channel(i);
    if (!is24GHz(ch)) { Serial.printf("[scan] ignore %s ch%d (5 GHz)\n", WiFi.BSSIDstr(i).c_str(), ch); continue; }
    int rssi = WiFi.RSSI(i);
    Serial.printf("[scan] %s ch%d rssi=%d\n", WiFi.BSSIDstr(i).c_str(), ch, rssi);
    if (rssi > bestRssi) { bestRssi = rssi; bestCh = ch; memcpy(bssidOut, WiFi.BSSID(i), 6); }
  }
  return bestCh;
}
static int scanFor24GHz(uint8_t* bssidOut) {
  int n = WiFi.scanNetworks( false,  false);
  int ch = pick24GHz(n, bssidOut);
  WiFi.scanDelete();
  return ch;
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
  cur = SCR_NONE;
}

void stopPortal() {
  if (!portalActive) return;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
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
    wifiFastSave(WiFi.channel(), WiFi.BSSID());
    stopPortal();
    MDNS.end();
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    if (!serverStarted) { server.begin(); serverStarted = true; }
    if (!vizUdpOn) { vizUdp.begin(VIZ_PORT); vizUdpOn = true; }
    if (!ntpConfigured) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      setenv("TZ", idleCfg.tz.c_str(), 1);
      tzset();
      ntpConfigured = true;
      Serial.println("[ntp] configuration lancee");
    }
    cur = SCR_NONE;
  }
  netWasUp = up;
  if (!up && wifiError != lastShownWifiErr) {
    lastShownWifiErr = wifiError;
    cur = SCR_NONE;
  }
  if (up && ntpConfigured && !ntpSynced && millis() - lastNtpRetry > 10000) {
    lastNtpRetry = millis();
    ensureNtp();
    if (ntpSynced) {
      Serial.println("[ntp] synchronise OK");
    }
  }
  if (!up && !portalActive && (wifiSsid.length() == 0 || (!everConnected && millis() > 90000UL))) startPortal();
  static bool     scanRunning = false;
  static uint32_t scanStart   = 0;
  if (up) { scanRunning = false; return; }
  if (wifiSsid.length()) {
    if (!scanRunning && millis() - lastTry > (portalActive ? 60000UL : 10000UL)) {
      lastTry = millis();
      Serial.println("[wifi] scan + reconnexion 2.4 GHz...");
      WiFi.disconnect();
      WiFi.scanNetworks( true,  false);
      scanRunning = true; scanStart = millis();
    } else if (scanRunning) {
      const int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING && millis() - scanStart < 12000UL) return;
      scanRunning = false;
      uint8_t bssid[6];
      const int ch = (n > 0) ? pick24GHz(n, bssid) : -1;
      WiFi.scanDelete();
      if (ch > 0) {
        Serial.printf("[wifi] connexion au BSSID 2.4 GHz, ch%d\n", ch);
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str(), ch, bssid);
      } else {
        Serial.println("[wifi] aucun AP 2.4 GHz trouve, essai sans BSSID");
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
      }
    }
  }
}

static void uiSwitchSprites(Screen s) {
  if (s == SCR_PLAY) {
    endModernDashboard();
    if (!barSpr.created())   barSpr.createSprite(BAR_W, BAR_H);
    if (!titleSpr.created()) titleSpr.createSprite(COL_W, TITLE_H);
    if (!barSpr.created() || !titleSpr.created()) Serial.println("[ui] RAM insuffisante pour les sprites de lecture");
  } else if (s == SCR_WAIT) {
    if (barSpr.created())   barSpr.deleteSprite();
    if (titleSpr.created()) titleSpr.deleteSprite();
    if (!modernDashboardReady() && !initModernDashboard()) Serial.println("[ui] RAM insuffisante pour le dashboard");
  } else {
    return;
  }
  Serial.printf("[ui] ecran %d : heap libre %u, plus grand bloc %u\n", (int)s, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

static void reserveStrings() {
  np.title.reserve(96); np.artist.reserve(64); np.album.reserve(64); np.coverId.reserve(24);
  np.app.reserve(24); np.appId.reserve(40);
  np.lyric.reserve(160); np.lyricPrev.reserve(160); np.lyricNext.reserve(160);
  coverIdStored.reserve(24); iconIdStored.reserve(40); bigIdStored.reserve(24);
  newsFullStr.reserve(1024);
  for (int i = 0; i < 8; i++) {
    idleData.news[i].reserve(112);
    idleData.newsMeta[i].desc.reserve(NEWS_DESC_MAX);
    idleData.newsMeta[i].when.reserve(14);
    idleData.newsMeta[i].src.reserve(32);
  }
}

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

  touchSPI.begin(25, 39, 32, 33);
  ts.begin(touchSPI);
  ts.setRotation(ROTATION);
  pinMode(LDR_PIN, INPUT);
  ledInit();
  ledPwm(0, 0, 0);
  if (cfg.sdCache) { sdMount(); touchBus(); }

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tftOutput);

  reserveStrings();
  coverBuf = (uint8_t*)malloc(COVER_MAX_BYTES);
  iconBuf  = (uint8_t*)malloc(ICON_MAX_BYTES);
  Serial.printf("[ui] heap libre apres sprites: %u (max bloc %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/nowplaying", HTTP_POST, handleNowPlaying);
  server.on("/api/cover", HTTP_POST, handleCover, uploadCover);
  server.on("/api/icon", HTTP_POST, handleIcon, uploadIcon);
  server.on("/api/coverbig", HTTP_POST, handleBigCover, uploadBig);
  server.on("/api/sdclear", HTTP_POST, handleSdClear);
  server.on("/api/config", HTTP_GET, sendConfig);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/diag", HTTP_GET, handleDiag);
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/api/idle", HTTP_POST, handleIdle);
  server.on("/api/idleconfig", HTTP_GET,  handleGetIdleConfig);
  server.on("/api/idleconfig", HTTP_POST, handlePostIdleConfig);
  server.on("/wifi", HTTP_POST, handlePortalSave);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/wifireset", HTTP_POST, handleWifiReset);
  server.onNotFound(handleNotFound);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

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

  if (wifiSsid.length() == 0) {
    startPortal();
  } else {
    uint8_t bssid[6]; int ch = 0;
    if (wifiFastLoad(bssid, &ch)) Serial.printf("[wifi] connexion rapide (AP memorise) ch%d\n", ch);
    else                          ch = scanFor24GHz(bssid);
    if (ch > 0) {
      Serial.printf("[wifi] demarrage sur BSSID 2.4 GHz ch%d\n", ch);
      WiFi.begin(wifiSsid.c_str(), wifiPass.c_str(), ch, bssid);
    } else {
      Serial.println("[wifi] demarrage sans BSSID specifique");
      WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
    }
  }
}

static inline void slowLog(const char* what, uint32_t t0) {
  const uint32_t d = millis() - t0;
  if (d >= 100) Serial.printf("[loop] %s a bloque %u ms (le tactile n'a pas ete lu pendant ce temps)\n", what, (unsigned)d);
}

static void loopBody() {
  uint32_t t0 = millis();
  wifiTask();                      slowLog("wifiTask", t0);
  if (portalActive) dnsServer.processNextRequest();
  t0 = millis(); tickNetFetch();   slowLog("meteo (fetch)", t0);
  t0 = millis(); if (serverStarted) server.handleClient(); slowLog("serveur HTTP", t0);
  pollViz();
  tickLdr();
  tickLed();
  diag::tick();

  Screen want;
  if (WiFi.status() != WL_CONNECTED) {
    want = SCR_WIFI;
  } else {
    bool timedOut = !hasTrack || millis() - lastRx >= IDLE_TIMEOUT_MS;
    bool pauseIdle = hasTrack && np.state == "paused" && pausedSince > 0
                     && millis() - pausedSince >= PAUSE_TO_IDLE_MS;
    if (pauseIdle) forcedIdle = true;
    if      (timedOut || forcedIdle) want = SCR_WAIT;
    else                             want = SCR_PLAY;
  }

  bool onDetailScreen = (cur == SCR_IDLE_WX || cur == SCR_IDLE_NEWS || cur == SCR_MENU || cur == SCR_SYS);
  if (!onDetailScreen && want != cur) {
    cur = want;
    lastClockMin = -1;
    fullCover = false;
    vizMode = false; lyricFull = false;
    bigRelease();
    uiSwitchSprites(cur);
    idleDrawn = false;
    if (cur == SCR_PLAY) drawMusicScreen();
    else if (cur == SCR_WIFI) drawWaitScreen(true);
  }

  if (cur == SCR_PLAY && !fullCover) {
    struct tm tm;
    if (timeNow(&tm) && tm.tm_min != lastClockMin) {
      lastClockMin = tm.tm_min;
      headerDirty = true;
    }
  }

  if (cur == SCR_PLAY && !fullCover && !vizMode && !lyricFull) {
    static uint32_t lastSprChk = 0;
    if (millis() - lastSprChk >= 3000UL) {
      lastSprChk = millis();
      if (!barSpr.created() || !titleSpr.created()) { uiSwitchSprites(SCR_PLAY); drawMusicScreen(); }
    }
  }

  tickAutoDim();

  if (lyricSwapAt && (int32_t)(millis() - lyricSwapAt) >= 0) {
    lyricSwapAt = 0;
    if (np.state == "playing" && np.lyricNext.length()) {
      np.lyricPrev = np.lyric; np.lyric = np.lyricNext; np.lyricNext = "";
      lyricDirty = true; lyricPage = 0; lyricPageMs = millis(); lyricSeen = true;
    }
  }

  if (cur == SCR_PLAY) {
    handleTouch();
    renderPlay();
  } else if (cur == SCR_WAIT) {
    handleTouch();
    if (cur == SCR_WAIT) animateIdle();
  } else if (cur == SCR_IDLE_WX || cur == SCR_IDLE_NEWS || cur == SCR_MENU || cur == SCR_SYS) {
    handleTouch();
    tickSysScreen();
    tickMenuScreen();
  } else {
    animateWait();
  }
}

void loop() {
  const uint32_t t = micros();
  loopBody();
  loopStatRecord(micros() - t);
}
