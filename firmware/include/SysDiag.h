// =============================================================================
//  SysDiag.h : diagnostic memoire (heap, fragmentation, fuites) pour ESP32 sans PSRAM
//
//  - diag::snap()        : instantane free / plus grand bloc contigu / minimum historique
//  - diag::HeapScope     : RAII. Log "avant" au constructeur, "apres" (+ delta) au destructeur,
//                          meme en cas de return anticipe. A poser en tete d'une fonction a risque.
//  - diag::tick()        : echantillonne le heap toutes les 5 s (courbe ecran Systeme) et detecte
//                          une derive continue (fuite) ou un plus grand bloc trop petit (fragmentation).
//  - diag::jpegErr()     : nom lisible d'un code JRESULT de TJpg_Decoder.
//  Compatible C++11 (core arduino-esp32 2.x) : pas de variable inline ni de lambda generique.
// =============================================================================
#pragma once
#include <Arduino.h>

// v1.8 : le port serie a 115200 bauds coute ~9 ms pour 100 caracteres. Les logs "avant/apres" etaient donc eux-memes
// une source de lenteur. Par defaut on ne logue que les anomalies (perte >= 4 Ko, bloc contigu faible).
// Mettre -DDIAG_VERBOSE=1 dans platformio.ini pour retrouver la trace complete.
#ifndef DIAG_VERBOSE
#define DIAG_VERBOSE 0
#endif

namespace diag {

static const int      HIST_N        = 48;       // 48 echantillons x 5 s = 4 min d'historique
static const uint32_t HIST_EVERY_MS = 5000;
static const uint32_t WARN_MAXBLK   = 24000;    // < 24 Ko contigus : un POST de pochette (base64 ~20 Ko) peut echouer

struct Heap { uint32_t freeB, maxBlk, minFree; };

inline Heap snap() {
  Heap h;
  h.freeB   = ESP.getFreeHeap();
  h.maxBlk  = ESP.getMaxAllocHeap();
  h.minFree = ESP.getMinFreeHeap();
  return h;
}

// 0 % = sain (un seul gros bloc) ; proche de 100 % = heap tres morcele
inline int fragPct(const Heap& h) {
  if (h.freeB == 0) return 0;
  uint32_t m = h.maxBlk > h.freeB ? h.freeB : h.maxBlk;
  return 100 - (int)(((uint64_t)m * 100ULL) / h.freeB);
}

// ---- Historique (tampon circulaire, etat dans des statics de fonction : C++11 OK) ----
struct History {
  uint32_t freeB[HIST_N];
  uint32_t maxBlk[HIST_N];
  int      n;       // nombre d'echantillons valides (<= HIST_N)
  int      head;    // prochaine case a ecrire
  uint32_t lastMs;
  uint32_t worstBlk;   // plus petit "plus grand bloc" jamais vu
  uint32_t minBlockWarnMs;
  uint32_t leakWarnMs;
};
inline History& hist() {
  static History h = { {0}, {0}, 0, 0, 0, 0xFFFFFFFFu, 0, 0 };
  return h;
}
// i = 0 -> le plus ancien echantillon
inline uint32_t histFree(int i)   { History& h = hist(); return h.freeB [(h.head - h.n + i + HIST_N * 2) % HIST_N]; }
inline uint32_t histMaxBlk(int i) { History& h = hist(); return h.maxBlk[(h.head - h.n + i + HIST_N * 2) % HIST_N]; }

inline void note(const Heap& s) {
  History& h = hist();
  if (s.maxBlk < h.worstBlk) h.worstBlk = s.maxBlk;
}

// Derive : moyenne des 6 plus vieux vs 6 plus recents echantillons (> 8 Ko perdus sur >= 3 min).
inline bool leakSuspected(int32_t* lossBytes = nullptr) {
  History& h = hist();
  if (h.n < 36) return false;
  uint32_t a = 0, b = 0;
  for (int i = 0; i < 6; i++) { a += histFree(i); b += histFree(h.n - 1 - i); }
  int32_t loss = (int32_t)(a / 6) - (int32_t)(b / 6);
  if (lossBytes) *lossBytes = loss;
  return loss > 8192;
}

// A appeler dans loop() : tres leger (sort immediatement tant que 5 s ne sont pas ecoulees).
inline void tick() {
  History& h = hist();
  const uint32_t now = millis();
  if (now - h.lastMs < HIST_EVERY_MS) return;
  h.lastMs = now;
  Heap s = snap();
  note(s);
  h.freeB[h.head]  = s.freeB;
  h.maxBlk[h.head] = s.maxBlk;
  h.head = (h.head + 1) % HIST_N;
  if (h.n < HIST_N) h.n++;
  if (s.maxBlk < WARN_MAXBLK && now - h.minBlockWarnMs > 60000UL) {   // 1 alerte / minute max
    h.minBlockWarnMs = now;
    Serial.printf("[heap] ALERTE fragmentation : plus grand bloc %u o (free %u o, frag %d%%)\n",
                  (unsigned)s.maxBlk, (unsigned)s.freeB, fragPct(s));
  }
  int32_t loss = 0;
  if (leakSuspected(&loss) && now - h.leakWarnMs > 60000UL) {   // 1 alerte / minute max
    h.leakWarnMs = now;
    Serial.printf("[heap] ALERTE derive : -%d o sur ~%d s -> fuite possible\n", (int)loss, h.n * 5);
  }
}

// ---- RAII : a poser en tete du parsing RSS, du chargement de pochette, etc. ----
class HeapScope {
 public:
  explicit HeapScope(const char* tag) : tag_(tag), before_(snap()) {
    note(before_);
    if (DIAG_VERBOSE)
      Serial.printf("[heap] > %-12s free=%u maxBlk=%u frag=%d%%\n", tag_,
                    (unsigned)before_.freeB, (unsigned)before_.maxBlk, fragPct(before_));
  }
  ~HeapScope() {
    Heap a = snap();
    note(a);
    const long dF = (long)((int32_t)a.freeB - (int32_t)before_.freeB);
    if (DIAG_VERBOSE || dF <= -4096 || a.maxBlk < WARN_MAXBLK)
    Serial.printf("[heap] < %-12s free=%u (%+ld) maxBlk=%u (%+ld) frag=%d%% %s\n", tag_,
                  (unsigned)a.freeB,   (long)((int32_t)a.freeB   - (int32_t)before_.freeB),
                  (unsigned)a.maxBlk,  (long)((int32_t)a.maxBlk  - (int32_t)before_.maxBlk),
                  fragPct(a), a.maxBlk < WARN_MAXBLK ? "!! BLOC FAIBLE" : "");
  }
 private:
  const char* tag_;
  Heap        before_;
  HeapScope(const HeapScope&);              // non copiable (C++11 sans =delete pour rester portable)
  HeapScope& operator=(const HeapScope&);
};

// Codes TJpgDec (JRESULT) -> texte
inline const char* jpegErr(int r) {
  switch (r) {
    case 0: return "OK";
    case 1: return "INTR";
    case 2: return "INP (flux tronque)";
    case 3: return "MEM1 (workspace trop petit)";
    case 4: return "MEM2 (tampon d'entree)";
    case 5: return "PAR";
    case 6: return "FMT1 (donnees corrompues)";
    case 7: return "FMT2 (non supporte)";
    case 8: return "FMT3 (norme non supportee, JPEG progressif ?)";
    default: return "?";
  }
}

// "1j 02h 03m" / "2h 03m 04s" / "5m 07s"
inline void fmtUptime(char* out, size_t n, uint32_t s) {
  uint32_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60, sec = s % 60;
  if (d)      snprintf(out, n, "%ud %02uh %02um", (unsigned)d, (unsigned)h, (unsigned)m);
  else if (h) snprintf(out, n, "%uh %02um %02us", (unsigned)h, (unsigned)m, (unsigned)sec);
  else        snprintf(out, n, "%um %02us", (unsigned)m, (unsigned)sec);
}

} // namespace diag
