#pragma once
#include <stdint.h>
#include <string.h>

namespace txt {

struct Line { uint16_t start, len; };

template <class WidthFn>
int wrap(const char* s, int maxW, WidthFn width, Line* out, int maxLines, bool* truncated = nullptr) {
  const int len = s ? (int)strlen(s) : 0;
  int n = 0, i = 0;
  while (i < len && n < maxLines) {
    while (i < len && s[i] == ' ') i++;
    if (i >= len) break;
    int lastFit = -1, j = i;
    while (j < len) {
      int k = j;
      while (k < len && s[k] != ' ') k++;
      if (width(s + i, k - i) > maxW) break;
      lastFit = k;
      j = k;
      while (j < len && s[j] == ' ') j++;
    }
    if (lastFit < 0) {
      int k = i + 1;
      while (k < len && width(s + i, k + 1 - i) <= maxW) k++;
      lastFit = k;
    }
    out[n].start = (uint16_t)i;
    out[n].len   = (uint16_t)(lastFit - i);
    n++;
    i = lastFit;
  }
  while (i < len && s[i] == ' ') i++;
  if (truncated) *truncated = (i < len);
  return n;
}

inline int pageCount(int totalLines, int linesPerPage) {
  if (linesPerPage <= 0 || totalLines <= 0) return 1;
  return (totalLines + linesPerPage - 1) / linesPerPage;
}

}
