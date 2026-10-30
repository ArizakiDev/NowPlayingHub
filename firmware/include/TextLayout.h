// =============================================================================
//  TextLayout.h : word-wrap + pagination SANS allocation ni dependance Arduino.
//  Pur C++ -> testable sur PC. La mesure de largeur est injectee (TFT_eSPI::textWidth
//  sur la carte, une fonction factice dans les tests).
//  Les lignes sont decrites par (debut, longueur) dans le texte source : zero copie.
// =============================================================================
#pragma once
#include <stdint.h>
#include <string.h>

namespace txt {

struct Line { uint16_t start, len; };

// Coupe `s` en lignes de largeur <= maxW. Coupe UNIQUEMENT aux espaces ; un mot plus
// large que la ligne est coupe de force en dernier recours (au moins 1 caractere par
// ligne -> pas de boucle infinie). `width(ptr, nChars)` renvoie la largeur en pixels.
// Retourne le nombre de lignes ecrites (<= maxLines) ; *truncated = true s'il reste du texte.
template <class WidthFn>
int wrap(const char* s, int maxW, WidthFn width, Line* out, int maxLines, bool* truncated = nullptr) {
  const int len = s ? (int)strlen(s) : 0;
  int n = 0, i = 0;
  while (i < len && n < maxLines) {
    while (i < len && s[i] == ' ') i++;                 // saute les espaces de tete
    if (i >= len) break;
    int lastFit = -1, j = i;
    while (j < len) {                                   // ajoute des mots tant que ca tient
      int k = j;
      while (k < len && s[k] != ' ') k++;               // mot = [j, k)
      if (width(s + i, k - i) > maxW) break;
      lastFit = k;
      j = k;
      while (j < len && s[j] == ' ') j++;
    }
    if (lastFit < 0) {                                  // mot unique trop large : coupe de force
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

} // namespace txt
