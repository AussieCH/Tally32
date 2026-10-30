/******************************************************************************
 * widgets.h  -  Knoepfe, Texte und Farben fuer Vollbild-Dialoge
 *               (Terminbuchung booking.cpp, Einstellungsmenue menu.cpp)
 *   Gestaltung in Graustufen wie die Statusanzeige, kantengeglaettete Schrift.
 ******************************************************************************/
#pragma once
#include "board.h"
#include "state.h"
#include "ui.h"

// ------------------------------------------------------------ Farben
// Wie die Statusanzeige (Oliver, 30.10.2026): weiss, Graustufen, hellgraue Kopfzeile.
// Zustaende von Knoepfen ueber die Helligkeit, nicht ueber Farbe.
inline uint32_t pBg()   { return 0xFFFFFF; }
inline uint32_t pInk()  { return 0x262626; }     // Text 85 % Schwarz
inline uint32_t pDim()  { return 0x737373; }     // Nebentext 55 %
inline uint32_t pBtn()  { return 0xEBEBEB; }     // Knopf, Taste 8 %
inline uint32_t pPri()  { return 0x404040; }     // Hauptknopf, gewaehlt 75 %
inline uint32_t pOff()  { return 0xF5F5F5; }     // nicht verfuegbar 4 %
inline uint32_t pField(){ return 0xF5F5F5; }     // Eingabefeld
inline uint32_t pHdr()  { return 0xCCCCCC; }     // Kopfzeile 20 %

// ------------------------------------------------------------ Text / Knoepfe
inline void txt(const char* s, int x, int y, int size, uint32_t fg, uint32_t bg,
                textdatum_t d = middle_center) {
  lcd.setFont(smooth(size));
  lcd.setTextColor(c565(fg), c565(bg));
  lcd.setTextDatum(d);
  lcd.drawString(s, x, y);
  lcd.setTextDatum(top_left);
}

struct Rect {
  int x, y, w, h;
  bool in(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  int cx() const { return x + w / 2; }
  int cy() const { return y + h / 2; }
};

enum Style { B_NORMAL, B_PRIMARY, B_DISABLED };
inline uint32_t btnBg(Style st) { return st == B_PRIMARY ? pPri() : st == B_DISABLED ? pOff() : pBtn(); }
inline uint32_t btnFg(Style st) { return st == B_PRIMARY ? 0xFFFFFF : st == B_DISABLED ? 0xADADAD : pInk(); }

inline void button(const Rect& r, const char* label, Style st, int size = 26) {
  lcd.fillRoundRect(r.x, r.y, r.w, r.h, us(8), c565(btnBg(st)));
  if (label && *label) txt(label, r.cx(), r.cy(), size, btnFg(st), btnBg(st));
}

// Kopfzeile wie die Statusanzeige: hellgrau, Titel links
inline void header(const char* t) {
  lcd.fillScreen(c565(pBg()));
  lcd.fillRect(0, 0, SCR_W, ly(56), c565(pHdr()));
  txt(t, lx(20), ly(28), 34, pInk(), pHdr(), middle_left);
}
