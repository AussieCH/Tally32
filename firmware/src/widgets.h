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

// B_PRESSED / B_PRESSED_PRI: kurz beim Antippen (normaler Knopf dunkel, Hauptknopf hell)
enum Style { B_NORMAL, B_PRIMARY, B_DISABLED, B_PRESSED, B_PRESSED_PRI };
inline uint32_t btnBg(Style st) {
  switch (st) {
    case B_PRIMARY:     return pPri();
    case B_DISABLED:    return pOff();
    case B_PRESSED:     return 0x595959;         // 65 %
    case B_PRESSED_PRI: return 0xA6A6A6;         // 35 %
    default:            return pBtn();
  }
}
inline uint32_t btnFg(Style st) {
  return st == B_PRIMARY || st == B_PRESSED ? 0xFFFFFF : st == B_DISABLED ? 0xADADAD : pInk();
}

// ------------------------------------------------------------ Antippen sichtbar machen
// Jeder gezeichnete Knopf merkt sich Lage, Text und Stil (pro Bildschirm, header() leert
// die Liste). Beim Antippen blitzt er kurz auf, dann folgt die Aktion. Bleibt der
// Bildschirm danach derselbe, zeichnet btnRelease() den Knopf wieder normal.
// Knoepfe mit eigenem Inhalt (Symbol, zwei Zeilen) melden mit btnCustom() ihre Zeichenfunktion.
typedef void (*BtnDraw)(const Rect& r, Style st, int ctx);
struct BtnReg { Rect r; Style st; int size; BtnDraw draw; int ctx; char label[28]; };
inline BtnReg btnRegs[64];
inline int btnN = 0, btnLast = -1;
inline uint32_t btnGen = 0;                      // zaehlt bei jedem neuen Bildschirm hoch
inline bool btnNoReg = false;                    // beim Aufblitzen nicht neu registrieren
inline constexpr uint32_t BTN_FLASH_MS = 90;

inline void btnClear() { btnN = 0; btnLast = -1; btnGen++; }

inline void btnRegister(const Rect& r, const char* label, Style st, int size) {
  if (btnNoReg) return;
  int i = 0;
  while (i < btnN && !(btnRegs[i].r.x == r.x && btnRegs[i].r.y == r.y && btnRegs[i].r.w == r.w && btnRegs[i].r.h == r.h)) i++;
  if (i == btnN) { if (btnN >= (int)(sizeof(btnRegs) / sizeof(btnRegs[0]))) { btnLast = -1; return; } btnN++; }
  BtnReg& b = btnRegs[i];
  b.r = r; b.st = st; b.size = size; b.draw = nullptr; b.ctx = 0;
  strncpy(b.label, label ? label : "", sizeof(b.label) - 1); b.label[sizeof(b.label) - 1] = '\0';
  btnLast = i;
}
inline void btnCustom(BtnDraw d, int ctx = 0) {
  if (btnNoReg || btnLast < 0) return;
  btnRegs[btnLast].draw = d; btnRegs[btnLast].ctx = ctx;
}

inline void button(const Rect& r, const char* label, Style st, int size = 26) {
  lcd.fillRoundRect(r.x, r.y, r.w, r.h, us(8), c565(btnBg(st)));
  if (label && *label) txt(label, r.cx(), r.cy(), size, btnFg(st), btnBg(st));
  btnRegister(r, label, st, size);
}

inline void btnDrawAs(const BtnReg& b, Style st) {
  btnNoReg = true;
  if (b.draw) b.draw(b.r, st, b.ctx);
  else button(b.r, b.label, st, b.size);
  btnNoReg = false;
}

// Knopf unter (x, y) aufblitzen lassen -> Index oder -1 (kein Knopf, oder ausgegraut)
inline int btnPress(int x, int y, uint32_t ms = BTN_FLASH_MS) {
  for (int i = btnN - 1; i >= 0; i--) {
    if (!btnRegs[i].r.in(x, y)) continue;
    if (btnRegs[i].st == B_DISABLED) return -1;
    btnDrawAs(btnRegs[i], btnRegs[i].st == B_PRIMARY ? B_PRESSED_PRI : B_PRESSED);
    delay(ms);
    return i;
  }
  return -1;
}
// Nach der Aktion: gleicher Bildschirm -> Knopf im aktuellen Stil wieder zeichnen
inline void btnRelease(int i, uint32_t gen) {
  if (i < 0 || gen != btnGen || i >= btnN) return;
  btnDrawAs(btnRegs[i], btnRegs[i].st);
}

// Kopfzeile wie die Statusanzeige: hellgrau, Titel links
inline void header(const char* t) {
  btnClear();
  lcd.fillScreen(c565(pBg()));
  lcd.fillRect(0, 0, SCR_W, ly(56), c565(pHdr()));
  txt(t, lx(20), ly(28), 34, pInk(), pHdr(), middle_left);
}
