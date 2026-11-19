#include "../firmware/include/TextLayout.h"
#include <cstdio>
#include <cassert>
#include <string>
static int W(const char*, int n){ return n * 8; }
static std::string L(const char* s, txt::Line l){ return std::string(s + l.start, l.len); }
int main(){
  txt::Line o[16]; bool tr;
  const char* a = "le chat dort sur le canape rouge";
  int n = txt::wrap(a, 12*8, W, o, 16, &tr);
  for(int i=0;i<n;i++) printf("[%s]\n", L(a,o[i]).c_str());
  assert(n==3 && L(a,o[0])=="le chat dort" && L(a,o[1])=="sur le" && !tr);
  const char* b = "supercalifragilistic ok";
  n = txt::wrap(b, 6*8, W, o, 16, &tr);
  assert(n>=4); for(int i=0;i<n;i++) assert(o[i].len>=1 && o[i].len<=6);
  n = txt::wrap(a, 12*8, W, o, 2, &tr); assert(n==2 && tr);
  assert(txt::wrap("   ", 80, W, o, 4)==0); assert(txt::wrap(nullptr, 80, W, o, 4)==0);
  const char* c = "a    b"; n = txt::wrap(c, 80, W, o, 4); assert(n==1 && o[0].len==6);
  assert(txt::pageCount(0,4)==1 && txt::pageCount(4,4)==1 && txt::pageCount(5,4)==2 && txt::pageCount(9,4)==3);
  puts("OK TextLayout");
}
