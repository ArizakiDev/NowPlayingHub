#pragma once
#include <Arduino.h>

#ifndef DIAG_VERBOSE
#define DIAG_VERBOSE 0
#endif

namespace diag {

static const int      HIST_N        = 48;
static const uint32_t HIST_EVERY_MS = 5000;
static const uint32_t WARN_MAXBLK   = 24000;

struct Heap { uint32_t freeB, maxBlk, minFree; };

inline Heap snap() {
  Heap h;
  h.freeB   = ESP.getFreeHeap();
  h.maxBlk  = ESP.getMaxAllocHeap();
  h.minFree = ESP.getMinFreeHeap();
  return h;
}

inline int fragPct(const Heap& h) {
  if (h.freeB == 0) return 0;
  uint32_t m = h.maxBlk > h.freeB ? h.freeB : h.maxBlk;
  return 100 - (int)(((uint64_t)m * 100ULL) / h.freeB);
}

struct History {
  uint32_t freeB[HIST_N];
  uint32_t maxBlk[HIST_N];
  int      n;
  int      head;
  uint32_t lastMs;
  uint32_t worstBlk;
  uint32_t minBlockWarnMs;
  uint32_t leakWarnMs;
};
inline History& hist() {
  static History h = { {0}, {0}, 0, 0, 0, 0xFFFFFFFFu, 0, 0 };
  return h;
}
inline uint32_t histFree(int i)   { History& h = hist(); return h.freeB [(h.head - h.n + i + HIST_N * 2) % HIST_N]; }
inline uint32_t histMaxBlk(int i) { History& h = hist(); return h.maxBlk[(h.head - h.n + i + HIST_N * 2) % HIST_N]; }

inline void note(const Heap& s) {
  History& h = hist();
  if (s.maxBlk < h.worstBlk) h.worstBlk = s.maxBlk;
}

inline bool leakSuspected(int32_t* lossBytes = nullptr) {
  History& h = hist();
  if (h.n < 36) return false;
  uint32_t a = 0, b = 0;
  for (int i = 0; i < 6; i++) { a += histFree(i); b += histFree(h.n - 1 - i); }
  int32_t loss = (int32_t)(a / 6) - (int32_t)(b / 6);
  if (lossBytes) *lossBytes = loss;
  return loss > 8192;
}

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
  if (s.maxBlk < WARN_MAXBLK && now - h.minBlockWarnMs > 60000UL) {
    h.minBlockWarnMs = now;
    Serial.printf("[heap] ALERTE fragmentation : plus grand bloc %u o (free %u o, frag %d%%)\n",
                  (unsigned)s.maxBlk, (unsigned)s.freeB, fragPct(s));
  }
  int32_t loss = 0;
  if (leakSuspected(&loss) && now - h.leakWarnMs > 60000UL) {
    h.leakWarnMs = now;
    Serial.printf("[heap] ALERTE derive : -%d o sur ~%d s -> fuite possible\n", (int)loss, h.n * 5);
  }
}

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
  HeapScope(const HeapScope&);
  HeapScope& operator=(const HeapScope&);
};

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

inline void fmtUptime(char* out, size_t n, uint32_t s) {
  uint32_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60, sec = s % 60;
  if (d)      snprintf(out, n, "%ud %02uh %02um", (unsigned)d, (unsigned)h, (unsigned)m);
  else if (h) snprintf(out, n, "%uh %02um %02us", (unsigned)h, (unsigned)m, (unsigned)sec);
  else        snprintf(out, n, "%um %02us", (unsigned)m, (unsigned)sec);
}

}
