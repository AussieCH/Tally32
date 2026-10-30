/******************************************************************************
 * menu.cpp  -  Einstellungsmenue am Geraet
 *
 * Oeffnen: 3 s auf die Tally32-Plakette oben links druecken, dann PIN
 * (4 Ziffern, Standard 0000, aendern hier oder im Webinterface). Funktioniert
 * ohne Agent und ohne WLAN, also auch am reinen USB-Netzteil.
 * Nach 5 falschen PINs ist das Menue 60 s gesperrt. 60 s ohne Beruehrung
 * schliessen das Menue.
 *
 * Seiten: Uebersicht, Demomodus (startet direkt), Anzeige (Helligkeit, Drehen),
 * Farben (Farbfelder mit Helligkeitswert), Terminbuchung (Knopf, Zeitfenster,
 * Wochentage, Mittagspause), Microsoft 365 (Konto, neue Anmeldung ueber den
 * Agent), Einrichtungs-WLAN, Info, PIN aendern.
 * Ein/Aus steht immer als Text auf dem Knopf, nie nur ueber die Farbe.
 ******************************************************************************/
#include "widgets.h"

enum MScreen { M_OFF, M_PIN, M_MAIN, M_DISPLAY, M_COLORS, M_BOOK, M_WIFI, M_INFO, M_O365 };
enum PinMode { P_ENTER, P_NEW1, P_NEW2 };

static MScreen scr = M_OFF;
static PinMode pinStep = P_ENTER;
static char pinBuf[5] = "", pinFirst[5] = "";
static const char* pinMsg = "";
static int pinFails = 0;
static uint32_t lockUntil = 0, lastTouch = 0;
static bool displayDirty = false;

static const uint32_t MENU_TIMEOUT_MS = 60000, LOCK_MS = 60000;
static const int PIN_MAX_FAILS = 5;

// ------------------------------------------------------------ gemeinsame Teile
static const Rect R_BACK  = { ux(20), SCR_H - uy(84), ux(240), uy(68) };
static const Rect R_RIGHT = { SCR_W - ux(300), SCR_H - uy(84), ux(280), uy(68) };

static int lumOf(uint32_t c) { return (299 * ((c >> 16) & 0xFF) + 587 * ((c >> 8) & 0xFF) + 114 * (c & 0xFF)) / 1000; }

static void label(const char* s, int y) { txt(s, ux(40), y, 26, pInk(), pBg(), middle_left); }

static void toggle(const Rect& r, bool on) { button(r, on ? "Ein" : "Aus", on ? B_PRIMARY : B_NORMAL); }

// [-] 08:00 [+] : Zeit in 15-Minuten-Schritten
struct Stepper { Rect minus, value, plus; };
static Stepper stepperAt(int x, int y) { return { { x, y, ux(56), uy(64) }, { x + ux(60), y, ux(112), uy(64) }, { x + ux(176), y, ux(56), uy(64) } }; }
static void drawStepper(const Stepper& s, int minutes, bool enabled) {
  Style st = enabled ? B_NORMAL : B_DISABLED;
  button(s.minus, "-", st, 34);
  button(s.plus, "+", st, 34);
  char t[8]; snprintf(t, sizeof(t), "%02d:%02d", minutes / 60, minutes % 60);
  uint32_t fb = enabled ? pField() : pOff();
  lcd.fillRoundRect(s.value.x, s.value.y, s.value.w, s.value.h, 12, c565(fb));
  txt(t, s.value.cx(), s.value.cy(), 26, enabled ? pInk() : pDim(), fb);
}
// -1 / +1 bei Treffer auf [-] / [+], sonst 0
static int stepperHit(const Stepper& s, int x, int y) { return s.minus.in(x, y) ? -1 : s.plus.in(x, y) ? 1 : 0; }

// ------------------------------------------------------------ PIN
static const int KEY_W = ux(150), KEY_H = uy(78), KEY_GAP = us(14);
static const int KEY_X0 = (SCR_W - (3 * KEY_W + 2 * KEY_GAP)) / 2, KEY_Y0 = uy(215);
static Rect keyRect(int k) {                     // k 0..11: 1-9, Loeschen, 0, (leer)
  int c = k % 3, r = k / 3;
  return { KEY_X0 + c * (KEY_W + KEY_GAP), KEY_Y0 + r * (KEY_H + KEY_GAP), KEY_W, KEY_H };
}
static bool locked() { return lockUntil && (int32_t)(lockUntil - millis()) > 0; }

static void drawPinDots() {
  const int n = strlen(pinBuf), cx0 = SCR_W / 2 - us(90), cy = uy(140);
  lcd.fillRect(cx0 - us(20), cy - us(20), us(220), us(40), c565(pBg()));
  for (int i = 0; i < 4; i++) {                  // gefuellt = eingegeben (Form, nicht Farbe)
    int cx = cx0 + i * us(60);
    if (i < n) lcd.fillCircle(cx, cy, us(14), c565(pInk()));
    else { lcd.fillCircle(cx, cy, us(14), c565(pInk())); lcd.fillCircle(cx, cy, us(12), c565(pBg())); }
  }
}

static void drawPin() {
  header(pinStep == P_ENTER ? "Einstellungen: PIN eingeben"
       : pinStep == P_NEW1  ? "Neuer PIN"
                            : "Neuen PIN wiederholen");
  drawPinDots();
  if (locked()) {
    txt("Zu viele Versuche. Bitte 1 Minute warten.", SCR_W / 2, uy(300), 26, pInk(), pBg());
  } else {
    if (*pinMsg) txt(pinMsg, SCR_W / 2, uy(186), 20, pInk(), pBg());
    static const char* K[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "L\xC3\xB6schen", "0", "" };
    for (int k = 0; k < 11; k++) button(keyRect(k), K[k], B_NORMAL, k == 9 ? 20 : 34);
  }
  button(R_BACK, "Abbrechen", B_NORMAL, 20);
}

// ------------------------------------------------------------ Uebersicht
static const int TILE_N = 8, TILE_W = (SCR_W - ux(40) - 3 * ux(16)) / 4, TILE_H = uy(118);
static Rect tileRect(int i) { return { ux(20) + (i % 4) * (TILE_W + ux(16)), uy(92) + (i / 4) * (TILE_H + uy(14)), TILE_W, TILE_H }; }

static void tile(int i, const char* t, const char* sub) {
  Rect r = tileRect(i);
  button(r, nullptr, B_NORMAL);
  txt(t, r.cx(), r.cy() - uy(16), 26, pInk(), pBtn());
  if (sub && *sub) txt(sub, r.cx(), r.cy() + uy(22), 20, mixRGB(pInk(), pBtn(), 30), pBtn());
}

static void drawMain() {
  header("Einstellungen");
  char b[24];
  tile(0, "Demomodus", demoMode ? "l\xC3\xA4uft, beenden" : "starten");
  snprintf(b, sizeof(b), "Helligkeit %d %%", brightness * 100 / 255);
  tile(1, "Anzeige", b);
  tile(2, "Farben", "Statusbalken");
  tile(3, "Terminbuchung", bookingEnabled ? "Knopf ein" : "Knopf aus");
  tile(4, "Microsoft 365", account[0] ? "Konto verbunden" : "kein Konto");
  tile(5, "WLAN", apOff ? "Einrichtung aus" : "Einrichtung ein");
  tile(6, "Info", "Version " TALLY32_VERSION " " TALLY32_EDITION);
  tile(7, "PIN \xC3\xA4ndern", "");
  button(R_RIGHT, "Schliessen", B_PRIMARY);
}

// ------------------------------------------------------------ Anzeige
static const Rect R_BR_MINUS = { ux(300), uy(120), ux(90), uy(72) }, R_BR_PLUS = { ux(810), uy(120), ux(90), uy(72) };
static const Rect R_FLIP = { ux(600), uy(250), ux(200), uy(72) };
static const Rect R_NAME = { ux(600), uy(360), ux(300), uy(72) };   // Name in der Kopfzeile aendern
static char nameWork[48];

static void onNameDone(bool ok) {
  if (!ok) return;
  ownerName = nameWork;
  displayApply(true);                            // speichert und zeichnet neu
}
static const int BAR_X = ux(410), BAR_W = ux(384);

static void drawBrightness() {
  const int y = R_BR_MINUS.cy() - us(12);
  lcd.fillRoundRect(BAR_X, y, BAR_W, us(24), us(12), c565(pOff()));
  int w = BAR_W * brightness / 255;
  if (w >= us(24)) lcd.fillRoundRect(BAR_X, y, w, us(24), us(12), c565(pInk()));
  char b[8]; snprintf(b, sizeof(b), "%d %%", brightness * 100 / 255);
  lcd.fillRect(ux(910), R_BR_MINUS.y, SCR_W - ux(910), R_BR_MINUS.h, c565(pBg()));
  txt(b, ux(962), R_BR_MINUS.cy(), 26, pInk(), pBg());
}

static void drawDisplay() {
  header("Anzeige");
  label("Helligkeit", R_BR_MINUS.cy());
  button(R_BR_MINUS, "-", B_NORMAL, 48);
  button(R_BR_PLUS, "+", B_NORMAL, 48);
  drawBrightness();
  label("Display um 180 Grad drehen", R_FLIP.cy());
  toggle(R_FLIP, flip);
  label("Name in der Kopfzeile", R_NAME.cy() - uy(16));
  txt(ownerName.length() ? ownerName.c_str() : "(leer, zeigt das Konto vom Agent)",
      ux(40), R_NAME.cy() + uy(18), 20, pDim(), pBg(), middle_left);
  button(R_NAME, "\xC3\x84ndern", B_NORMAL);
  button(R_BACK, "Zur\xC3\xBC" "ck", B_NORMAL);
}

// ------------------------------------------------------------ Farben
// Je Zustand sechs Farben im passenden Helligkeitsbereich (frei hell, besetzt
// mittel, Gespraech und abwesend dunkel), damit sich die Zustaende auch ohne
// Farbsehen ueber die Helligkeit unterscheiden. Die erste ist der Standard.
static const int SW_N = 6;
struct ColorRow { const char* name; uint32_t Colors::* field; uint32_t sw[SW_N]; };
static const int ROW_N = 4;
static const ColorRow ROWS[ROW_N] = {   // Statusfarben (Balken), die linke Kachel gibt es seit 30.10.2026 nicht mehr
  { "Frei",                         &Colors::free_, { 0xA8E6A0, 0xC8F0C0, 0x9FE0D0, 0xD8F0A0, 0xB0D8F8, 0xF0F0F0 } },
  { "Besetzt",                      &Colors::busy,  { 0xE06A00, 0xD08020, 0xC05A30, 0xB8860B, 0x9A6AC0, 0x808890 } },
  { "Abwesend",                     &Colors::away,  { 0x1A2A6A, 0x2A1A5A, 0x103A4A, 0x3A3A48, 0x1A4A2A, 0x4A2A10 } },
  { "Im Gespr\xC3\xA4" "ch",        &Colors::call,  { 0x8C0810, 0xA01020, 0x701018, 0x9A1050, 0x6A0A30, 0xB02000 } },
};
static const int SW_X0 = ux(300), SW_W = ux(104), SW_H = uy(64), SW_GAP = ux(12), SW_Y0 = uy(88), SW_ROW = uy(84);
static Rect swatchRect(int r, int i) { return { SW_X0 + i * (SW_W + SW_GAP), SW_Y0 + r * SW_ROW, SW_W, SW_H }; }

static void drawColors() {
  header("Farben");
  for (int r = 0; r < ROW_N; r++) {
    label(ROWS[r].name, SW_Y0 + r * SW_ROW + SW_H / 2);
    uint32_t curC = colors.*(ROWS[r].field);
    for (int i = 0; i < SW_N; i++) {
      Rect q = swatchRect(r, i);
      uint32_t c = ROWS[r].sw[i];
      if (c == curC) lcd.fillRoundRect(q.x - us(6), q.y - us(6), q.w + us(12), q.h + us(12), 16, c565(pInk()));   // Rahmen = gewaehlt
      lcd.fillRoundRect(q.x, q.y, q.w, q.h, 12, c565(c));
      char p[8]; snprintf(p, sizeof(p), "%d %%", lumOf(c) * 100 / 255);
      uint32_t fg = lumOf(c) > 120 ? 0x000000 : 0xFFFFFF;
      txt(p, q.cx(), q.cy(), 20, fg, c);
    }
  }
  button(R_BACK, "Zur\xC3\xBC" "ck", B_NORMAL);
  button(R_RIGHT, "Standardfarben", B_NORMAL, 20);
}

// ------------------------------------------------------------ Terminbuchung
static const Rect R_BK_ON = { ux(560), uy(96), ux(180), uy(64) };
static const Stepper ST_FROM = stepperAt(ux(300), uy(184)), ST_TO = stepperAt(ux(600), uy(184));
static const int DAY_X0 = ux(300), DAY_W = ux(84), DAY_GAP = ux(10), DAY_Y = uy(272);
static const Rect R_LUNCH = { ux(300), uy(360), ux(120), uy(64) };
static const Stepper ST_LFROM = stepperAt(ux(440), uy(360)), ST_LTO = stepperAt(ux(740), uy(360));
static const char* WD[7] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };
static Rect dayBtn(int i) { return { DAY_X0 + i * (DAY_W + DAY_GAP), DAY_Y, DAY_W, uy(64) }; }

static void drawBook() {
  header("Terminbuchung am Display");
  label("Knopf Termin buchen", R_BK_ON.cy());
  toggle(R_BK_ON, bookingEnabled);
  label("Zeitfenster", ST_FROM.value.cy());
  drawStepper(ST_FROM, bookFrom, true);
  txt("bis", ux(566), ST_FROM.value.cy(), 26, pInk(), pBg());
  drawStepper(ST_TO, bookTo, true);
  label("Wochentage", DAY_Y + uy(32));
  for (int i = 0; i < 7; i++) {
    bool on = bookDays & (1 << i);
    button(dayBtn(i), WD[i], on ? B_PRIMARY : B_NORMAL);
    if (!on) {                                   // aus: zusaetzlich durchgestrichen (nicht nur Farbe)
      Rect r = dayBtn(i);
      lcd.drawWideLine(r.x + ux(18), r.cy(), r.x + r.w - ux(18), r.cy(), 2, c565(pDim()));
    }
  }
  label("Mittagspause", R_LUNCH.cy());
  toggle(R_LUNCH, lunchOn);
  drawStepper(ST_LFROM, lunchFrom, lunchOn);
  txt("bis", ux(706), ST_LFROM.value.cy(), 26, lunchOn ? pInk() : pDim(), pBg());
  drawStepper(ST_LTO, lunchTo, lunchOn);
  txt("Besucher buchen nur in diesem Zeitfenster und an diesen Tagen.", SCR_W / 2, uy(462), 20, pDim(), pBg());
  button(R_BACK, "Zur\xC3\xBC" "ck", B_NORMAL);
}

// ------------------------------------------------------------ Einrichtungs-WLAN
static void kv(const char* k, const char* v, int y) {
  txt(k, ux(60), y, 26, pDim(), pBg(), middle_left);
  txt(v, ux(380), y, 26, pInk(), pBg(), middle_left);
}

static void drawWifi() {
  header("Einrichtungs-WLAN");
  kv("Zustand", apOff ? "ausgeschaltet" : (webApState() == 1 ? "eingeschaltet" : "Start fehlgeschlagen"), uy(120));
  kv("Netz", webSsid(), uy(175));
  kv("Passwort", apPassLoad().c_str(), uy(230));
  kv("Webinterface", "http://192.168.4.1", uy(285));
  txt("Ohne Einrichtungs-WLAN geht alles auch hier im Men\xC3\xBC.", SCR_W / 2, uy(420), 20, pDim(), pBg());
  button(R_BACK, "Zur\xC3\xBC" "ck", B_NORMAL);
  button(R_RIGHT, apOff ? "Einschalten" : "Ausschalten", B_PRIMARY);
}

// ------------------------------------------------------------ Microsoft 365
static const char* o365Msg = "";

static void drawO365() {
  header("Microsoft 365");
  kv("Konto", account[0] ? account : "keins angemeldet", uy(120));
  kv("Kalender schreiben", canWrite ? "ja (Buchung m\xC3\xB6glich)" : "nein", uy(175));
  kv("Verbindung", linked ? "Agent verbunden" : "kein Agent", uy(230));
  txt("Die Anmeldung l\xC3\xA4uft \xC3\xBC" "ber den Agenten, Code und Adresse erscheinen hier.",
      SCR_W / 2, uy(320), 20, pDim(), pBg());
  if (*o365Msg) txt(o365Msg, SCR_W / 2, uy(370), 26, pInk(), pBg());
  button(R_BACK, "Zur\xC3\xBC" "ck", B_NORMAL);
  button(R_RIGHT, "Neues Konto verbinden", !linked ? B_DISABLED : B_PRIMARY, 20);
}

// ------------------------------------------------------------ Info
static void drawInfo() {
  header("Info");
  int y = uy(100);
  const int step = uy(44);
  kv("Version", TALLY32_VERSION " " TALLY32_EDITION, y); y += step;
#if defined(BOARD_JC8012)
  kv("Display", "GUITION JC8012P4A1C, 1280 x 800", y);
#else
  kv("Display", "GUITION JC1060P470C, 1024 x 600", y);
#endif
  y += step;
  kv("Verbindung", demoMode ? "Demomodus" : linked ? "Agent verbunden" : "kein Agent", y); y += step;
  kv("Konto", account[0] ? account : "keins angemeldet", y); y += step;
  kv("Kalender schreiben", canWrite ? "ja (Buchung m\xC3\xB6glich)" : "nein", y); y += step;
  char b[64];                                   // stand frueher unten auf der Statusanzeige
  if (apOff) snprintf(b, sizeof(b), "%s, aus", webSsid());
  else snprintf(b, sizeof(b), "%s, ein, http://192.168.4.1", webSsid());
  kv("Einrichtungs-WLAN", b, y); y += step;
  kv("Echtzeituhr", rtcState == 1 ? "l\xC3\xA4uft" : rtcState == 2 ? "Zeit verloren (Knopfzelle?)" : "nicht gefunden", y); y += step;
  kv("Letzter Neustart", resetReasonText(), y); y += step;
  button(R_BACK, "Zur\xC3\xBC" "ck", B_NORMAL);
}

// ------------------------------------------------------------ Ablauf
static void go(MScreen s) {
  scr = s;
  switch (s) {
    case M_PIN:     drawPin(); break;
    case M_MAIN:    drawMain(); break;
    case M_DISPLAY: drawDisplay(); break;
    case M_COLORS:  drawColors(); break;
    case M_BOOK:    drawBook(); break;
    case M_WIFI:    drawWifi(); break;
    case M_INFO:    drawInfo(); break;
    case M_O365:    drawO365(); break;
    default:        break;
  }
}

static void menuClose() {
  if (displayDirty) { displayApply(true); displayDirty = false; }
  scr = M_OFF;
  uiVersion++;                                   // Statusanzeige neu zeichnen
}

bool menuActive() { return scr != M_OFF; }

void menuOpen() {
  lastTouch = millis();
  pinStep = P_ENTER;
  pinBuf[0] = '\0';
  pinMsg = "";
  go(M_PIN);
}

static void pinDigit(char d) {
  size_t n = strlen(pinBuf);
  if (n >= 4) return;
  pinBuf[n] = d; pinBuf[n + 1] = '\0';
  drawPinDots();
  if (n + 1 < 4) return;
  delay(150);                                    // letzter Punkt kurz sichtbar
  switch (pinStep) {
    case P_ENTER:
      if (String(pinBuf) == pinLoad()) { pinFails = 0; go(M_MAIN); return; }
      if (++pinFails >= PIN_MAX_FAILS) { pinFails = 0; lockUntil = millis() + LOCK_MS; }
      pinMsg = "PIN falsch";
      break;
    case P_NEW1:
      strcpy(pinFirst, pinBuf);
      pinStep = P_NEW2;
      pinMsg = "";
      break;
    case P_NEW2:
      if (!strcmp(pinFirst, pinBuf)) {
        pinSave(pinBuf);
        pinBuf[0] = '\0';
        go(M_MAIN);
        txt("PIN ge\xC3\xA4ndert", SCR_W / 2, uy(80), 20, pInk(), pBg());
        return;
      }
      pinStep = P_NEW1;
      pinMsg = "Die beiden PIN stimmen nicht \xC3\xBC" "berein. Bitte neu eingeben.";
      break;
  }
  pinBuf[0] = '\0';
  drawPin();
}

static void tapPin(int x, int y) {
  if (R_BACK.in(x, y)) { if (pinStep == P_ENTER) menuClose(); else go(M_MAIN); return; }
  if (locked()) return;
  for (int k = 0; k < 11; k++) {
    if (!keyRect(k).in(x, y)) continue;
    if (k == 9) {                                // Loeschen
      size_t n = strlen(pinBuf);
      if (n) { pinBuf[n - 1] = '\0'; drawPinDots(); }
      return;
    }
    pinDigit(k == 10 ? '0' : (char)('1' + k));
    return;
  }
}

static void tapMain(int x, int y) {
  if (R_RIGHT.in(x, y)) { menuClose(); return; }
  for (int i = 0; i < TILE_N; i++) {
    if (!tileRect(i).in(x, y)) continue;
    switch (i) {
      case 0: demoMode = !demoMode; demoSave(); menuClose(); return;   // Demo gleich zeigen
      case 1: go(M_DISPLAY); return;
      case 2: go(M_COLORS); return;
      case 3: go(M_BOOK); return;
      case 4: o365Msg = ""; go(M_O365); return;
      case 5: go(M_WIFI); return;
      case 6: go(M_INFO); return;
      case 7: pinStep = P_NEW1; pinBuf[0] = '\0'; pinMsg = ""; go(M_PIN); return;
    }
  }
}

static void tapDisplay(int x, int y) {
  if (R_BACK.in(x, y)) {
    if (displayDirty) { displayApply(true); displayDirty = false; }
    go(M_MAIN);
    return;
  }
  int d = R_BR_MINUS.in(x, y) ? -1 : R_BR_PLUS.in(x, y) ? 1 : 0;
  if (d) {
    int b = brightness + d * 25;
    brightness = b < 12 ? 12 : (b > 255 ? 255 : b);
    ledcWrite(TFT_BL, brightness);
    displayDirty = true;
    drawBrightness();
    return;
  }
  if (R_NAME.in(x, y)) {                         // Menue schliessen, Tastatur der Buchung nutzen
    strncpy(nameWork, ownerName.c_str(), sizeof(nameWork) - 1); nameWork[sizeof(nameWork) - 1] = '\0';
    menuClose();
    textEntryOpen("Name in der Kopfzeile", nameWork, 41, onNameDone);   // 40 Zeichen wie im Webinterface
    return;
  }
  if (R_FLIP.in(x, y)) {
    flip = !flip;
    board_set_rotation(flip);
    displayDirty = true;
    drawDisplay();                               // gedreht neu zeichnen
  }
}

static void tapColors(int x, int y) {
  if (R_BACK.in(x, y)) { go(M_MAIN); return; }
  if (R_RIGHT.in(x, y)) { colors = DEFAULT_COLORS; colorsSave(); drawColors(); return; }
  for (int r = 0; r < ROW_N; r++)
    for (int i = 0; i < SW_N; i++)
      if (swatchRect(r, i).in(x, y)) {
        colors.*(ROWS[r].field) = ROWS[r].sw[i];
        colorsSave();
        drawColors();                            // linke Kachel faerbt das ganze Menue
        return;
      }
}

static void tapBook(int x, int y) {
  if (R_BACK.in(x, y)) { bookingSave(); go(M_MAIN); return; }
  if (R_BK_ON.in(x, y)) { bookingEnabled = !bookingEnabled; drawBook(); return; }
  int d;
  if ((d = stepperHit(ST_FROM, x, y))) {
    int v = bookFrom + d * 15;
    if (v >= 0 && v <= bookTo - 15) bookFrom = v;
    drawBook(); return;
  }
  if ((d = stepperHit(ST_TO, x, y))) {
    int v = bookTo + d * 15;
    if (v >= bookFrom + 15 && v <= 24 * 60) bookTo = v;
    drawBook(); return;
  }
  for (int i = 0; i < 7; i++)
    if (dayBtn(i).in(x, y)) {
      uint8_t m = bookDays ^ (1 << i);
      if (m) bookDays = m;                       // mindestens ein Tag bleibt
      drawBook(); return;
    }
  if (R_LUNCH.in(x, y)) { lunchOn = !lunchOn; drawBook(); return; }
  if (!lunchOn) return;
  if ((d = stepperHit(ST_LFROM, x, y))) {
    int v = lunchFrom + d * 15;
    if (v >= 0 && v <= lunchTo - 15) lunchFrom = v;
    drawBook(); return;
  }
  if ((d = stepperHit(ST_LTO, x, y))) {
    int v = lunchTo + d * 15;
    if (v >= lunchFrom + 15 && v <= 24 * 60) lunchTo = v;
    drawBook(); return;
  }
}

static void tapWifi(int x, int y) {
  if (R_BACK.in(x, y)) { go(M_MAIN); return; }
  if (R_RIGHT.in(x, y)) {
    apOff = !apOff;
    apOffSave();
    apApply();
    drawWifi();
  }
}

static void tapO365(int x, int y) {
  if (R_BACK.in(x, y)) { go(M_MAIN); return; }
  if (!R_RIGHT.in(x, y)) return;
  if (!linked) { o365Msg = "Kein Agent verbunden."; drawO365(); return; }
  menuClose();                                   // Anmeldebildschirm zeigt Code und Adresse
  login365Start();
}

void menuTap(int x, int y) {
  lastTouch = millis();
  switch (scr) {
    case M_PIN:     tapPin(x, y); break;
    case M_MAIN:    tapMain(x, y); break;
    case M_DISPLAY: tapDisplay(x, y); break;
    case M_COLORS:  tapColors(x, y); break;
    case M_BOOK:    tapBook(x, y); break;
    case M_WIFI:    tapWifi(x, y); break;
    case M_INFO:    if (R_BACK.in(x, y)) go(M_MAIN); break;
    case M_O365:    tapO365(x, y); break;
    default:        break;
  }
}

void menuLoop() {
  if (scr == M_OFF) return;
  if (millis() - lastTouch > MENU_TIMEOUT_MS) {
    if (scr == M_BOOK) bookingSave();
    menuClose();
    return;
  }
  // Sperre abgelaufen: Tastenfeld wieder zeigen
  static bool wasLocked = false;
  bool l = scr == M_PIN && locked();
  if (wasLocked && !l && scr == M_PIN) drawPin();
  wasLocked = l;
}
