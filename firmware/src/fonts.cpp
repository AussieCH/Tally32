/******************************************************************************
 * fonts.cpp  -  kantengeglaettete VLW-Schriften (Montserrat) aus dem Flash
 ******************************************************************************/
#include <LovyanGFX.hpp>
#include "fonts_vlw.h"
#if defined(BOARD_JC8012)
#include "fonts_vlw10.h"
#endif
#include "ui.h"

namespace {
struct Smooth {
  const uint8_t* data;
  uint32_t len;
  lgfx::PointerWrapper wrap;
  lgfx::VLWfont font;
  bool loaded = false;
};
Smooth S20{ VLW_20, sizeof(VLW_20) }, S26{ VLW_26, sizeof(VLW_26) },
       S34{ VLW_34, sizeof(VLW_34) }, S48{ VLW_48, sizeof(VLW_48) },
       S100{ VLW_100, sizeof(VLW_100) };           // nur Ziffern, ':', '.', '-' 
#if defined(BOARD_JC8012)
// 10": alles um 4/3 groesser (800 statt 600 Zeilen). Die Groessen im Code bleiben die
// des 7"-Layouts (20/26/34/48/100), hier auf die naechste groessere Schrift abgebildet.
Smooth S46{ VLW_46, sizeof(VLW_46) }, S64{ VLW_64, sizeof(VLW_64) }, S134{ VLW_134, sizeof(VLW_134) };
#endif
}

const lgfx::IFont* smooth(int size) {
#if defined(BOARD_JC8012)
  Smooth& s = size >= 100 ? S134 : size >= 48 ? S64 : size >= 34 ? S46 : size >= 26 ? S34 : S26;
#else
  Smooth& s = size >= 100 ? S100 : size >= 48 ? S48 : size >= 34 ? S34 : size >= 26 ? S26 : S20;
#endif
  if (!s.loaded) {                               // Metriken einmalig ins RAM
    s.wrap.set(s.data, s.len);
    s.font.loadFont(&s.wrap);
    s.loaded = true;
  }
  return &s.font;
}
