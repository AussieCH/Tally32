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
 * Agent), Einrichtungs-WLAN, Info, PIN aendern, Sprache / Language.
 * Ein/Aus steht immer als Text auf dem Knopf, nie nur ueber die Farbe.
 * Alle Texte zweisprachig ueber TR("deutsch", "english") aus ui.h.
 ******************************************************************************/
#include "widgets.h"

enum MScreen { M_OFF, M_PIN, M_MAIN, M_DISPLAY, M_COLORS, M_BOOK, M_WIFI, M_INFO, M_O365, M_LANG };
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

static void toggle(const Rect& r, bool on) { button(r, on ? TR("Ein", "On") : TR("Aus", "Off"), on ? B_PRIMARY : B_NORMAL); }

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
  header(pinStep == P_ENTER ? TR("Einstellungen: PIN eingeben", "Settings: enter PIN")
       : pinStep == P_NEW1  ? TR("Neuer PIN", "New PIN")
                            : TR("Neuen PIN wiederholen", "Repeat new PIN"));
  drawPinDots();
  if (locked()) {
    txt(TR("Zu viele Versuche. Bitte 1 Minute warten.", "Too many attempts. Please wait 1 minute."),
        SCR_W / 2, uy(300), 26, pInk(), pBg());
  } else {
    if (*pinMsg) txt(pinMsg, SCR_W / 2, uy(186), 20, pInk(), pBg());
    static const char* K[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", "" };
    for (int k = 0; k < 11; k++)
      button(keyRect(k), k == 9 ? TR("L\xC3\xB6schen", "Delete") : K[k], B_NORMAL, k == 9 ? 20 : 34);
  }
  button(R_BACK, TR("Abbrechen", "Cancel"), B_NORMAL, 20);
}

// ------------------------------------------------------------ Uebersicht
// 9 Kacheln, 4 Spalten: dritte Reihe (nur Sprache) endet bei uy(474), "Schliessen" beginnt bei uy(516)
static const int TILE_N = 9, TILE_W = (SCR_W - ux(40) - 3 * ux(16)) / 4, TILE_H = uy(118);
static Rect tileRect(int i) { return { ux(20) + (i % 4) * (TILE_W + ux(16)), uy(92) + (i / 4) * (TILE_H + uy(14)), TILE_W, TILE_H }; }

static void tile(int i, const char* t, const char* sub) {
  Rect r = tileRect(i);
  button(r, nullptr, B_NORMAL);
  lcd.setFont(smooth(26));                       // lange Titel ("Sprache / Language") eine Stufe kleiner
  int ts = lcd.textWidth(t) > r.w - us(16) ? 20 : 26;
  txt(t, r.cx(), r.cy() - uy(16), ts, pInk(), pBtn());
  if (sub && *sub) txt(sub, r.cx(), r.cy() + uy(22), 20, mixRGB(pInk(), pBtn(), 30), pBtn());
}

static void drawMain() {
  header(TR("Einstellungen", "Settings"));
  char b[24];
  tile(0, TR("Demomodus", "Demo mode"), demoMode ? TR("l\xC3\xA4uft, beenden", "running, stop") : TR("starten", "start"));
  snprintf(b, sizeof(b), TR("Helligkeit %d %%", "Brightness %d %%"), brightness * 100 / 255);
  tile(1, TR("Anzeige", "Display"), b);
  tile(2, TR("Farben", "Colours"), TR("Statusbalken", "Status bar"));
  tile(3, TR("Terminbuchung", "Booking"), bookingEnabled ? TR("Knopf ein", "Button on") : TR("Knopf aus", "Button off"));
  tile(4, "Microsoft 365", account[0] ? TR("Konto verbunden", "Account linked") : TR("kein Konto", "no account"));
  tile(5, TR("WLAN", "Wi-Fi"), apOff ? TR("Einrichtung aus", "Setup off") : TR("Einrichtung ein", "Setup on"));
  tile(6, "Info", "Version " TALLY32_VERSION " " TALLY32_EDITION);
  tile(7, TR("PIN \xC3\xA4ndern", "Change PIN"), "");
  tile(8, "Sprache / Language", uiLang ? "English" : "Deutsch");
  button(R_RIGHT, TR("Schliessen", "Close"), B_PRIMARY);
}

// ------------------------------------------------------------ Sprache / Language
// Immer in beiden Sprachen beschriftet, damit man auch zurueckfindet.
static const Rect R_LANG_DE = { (SCR_W - ux(600)) / 2, uy(200), ux(280), uy(100) };
static const Rect R_LANG_EN = { (SCR_W - ux(600)) / 2 + ux(320), uy(200), ux(280), uy(100) };

static void drawLang() {
  header("Sprache / Language");
  button(R_LANG_DE, "Deutsch", uiLang == 0 ? B_PRIMARY : B_NORMAL, 34);
  button(R_LANG_EN, "English", uiLang == 1 ? B_PRIMARY : B_NORMAL, 34);
  txt(TR("Gilt f\xC3\xBCr Display, Webinterface und die Texte des Agenten.", "Applies to the display, web interface and agent texts."),
      SCR_W / 2, uy(360), 20, pDim(), pBg());
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
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
  header(TR("Anzeige", "Display"));
  label(TR("Helligkeit", "Brightness"), R_BR_MINUS.cy());
  button(R_BR_MINUS, "-", B_NORMAL, 48);
  button(R_BR_PLUS, "+", B_NORMAL, 48);
  drawBrightness();
  label(TR("Display um 180 Grad drehen", "Rotate display 180 degrees"), R_FLIP.cy());
  toggle(R_FLIP, flip);
  label(TR("Name in der Kopfzeile", "Name in the header"), R_NAME.cy() - uy(16));
  txt(ownerName.length() ? ownerName.c_str() : TR("(leer, zeigt das Konto vom Agent)", "(empty, shows the agent's account)"),
      ux(40), R_NAME.cy() + uy(18), 20, pDim(), pBg(), middle_left);
  button(R_NAME, TR("\xC3\x84ndern", "Change"), B_NORMAL);
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
}

// ------------------------------------------------------------ Farben
// Je Zustand sechs Farben im passenden Helligkeitsbereich (frei hell, besetzt
// mittel, Gespraech und abwesend dunkel), damit sich die Zustaende auch ohne
// Farbsehen ueber die Helligkeit unterscheiden. Die erste ist der Standard.
static const int SW_N = 6;
struct ColorRow { const char* name; const char* nameEn; uint32_t Colors::* field; uint32_t sw[SW_N]; };
static const int ROW_N = 4;
static const ColorRow ROWS[ROW_N] = {   // Statusfarben (Balken), die linke Kachel gibt es seit 30.10.2026 nicht mehr
  { "Frei",                  "Free",      &Colors::free_, { 0xA8E6A0, 0xC8F0C0, 0x9FE0D0, 0xD8F0A0, 0xB0D8F8, 0xF0F0F0 } },
  { "Besetzt",               "Busy",      &Colors::busy,  { 0xE06A00, 0xD08020, 0xC05A30, 0xB8860B, 0x9A6AC0, 0x808890 } },
  { "Abwesend",              "Away",      &Colors::away,  { 0x1A2A6A, 0x2A1A5A, 0x103A4A, 0x3A3A48, 0x1A4A2A, 0x4A2A10 } },
  { "Im Gespr\xC3\xA4" "ch", "On a call", &Colors::call,  { 0x8C0810, 0xA01020, 0x701018, 0x9A1050, 0x6A0A30, 0xB02000 } },
};
static const int SW_X0 = ux(300), SW_W = ux(104), SW_H = uy(64), SW_GAP = ux(12), SW_Y0 = uy(88), SW_ROW = uy(84);
static Rect swatchRect(int r, int i) { return { SW_X0 + i * (SW_W + SW_GAP), SW_Y0 + r * SW_ROW, SW_W, SW_H }; }

static void drawColors() {
  header(TR("Farben", "Colours"));
  for (int r = 0; r < ROW_N; r++) {
    label(TR(ROWS[r].name, ROWS[r].nameEn), SW_Y0 + r * SW_ROW + SW_H / 2);
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
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
  button(R_RIGHT, TR("Standardfarben", "Default colours"), B_NORMAL, 20);
}

// ------------------------------------------------------------ Terminbuchung
static const Rect R_BK_ON = { ux(560), uy(96), ux(180), uy(64) };
static const Stepper ST_FROM = stepperAt(ux(300), uy(184)), ST_TO = stepperAt(ux(600), uy(184));
static const int DAY_X0 = ux(300), DAY_W = ux(84), DAY_GAP = ux(10), DAY_Y = uy(272);
static const Rect R_LUNCH = { ux(300), uy(360), ux(120), uy(64) };
static const Stepper ST_LFROM = stepperAt(ux(440), uy(360)), ST_LTO = stepperAt(ux(740), uy(360));
static const char* WD_DE[7] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };
static const char* WD_EN[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
static const char* wdName(int i) { return (uiLang ? WD_EN : WD_DE)[i % 7]; }
static Rect dayBtn(int i) { return { DAY_X0 + i * (DAY_W + DAY_GAP), DAY_Y, DAY_W, uy(64) }; }

static void drawBook() {
  header(TR("Terminbuchung am Display", "Booking at the display"));
  label(TR("Knopf Termin buchen", "Booking button"), R_BK_ON.cy());
  toggle(R_BK_ON, bookingEnabled);
  label(TR("Zeitfenster", "Time window"), ST_FROM.value.cy());
  drawStepper(ST_FROM, bookFrom, true);
  txt(TR("bis", "to"), ux(566), ST_FROM.value.cy(), 26, pInk(), pBg());
  drawStepper(ST_TO, bookTo, true);
  label(TR("Wochentage", "Weekdays"), DAY_Y + uy(32));
  for (int i = 0; i < 7; i++) {
    bool on = bookDays & (1 << i);
    button(dayBtn(i), wdName(i), on ? B_PRIMARY : B_NORMAL);
    if (!on) {                                   // aus: zusaetzlich durchgestrichen (nicht nur Farbe)
      Rect r = dayBtn(i);
      lcd.drawWideLine(r.x + ux(18), r.cy(), r.x + r.w - ux(18), r.cy(), 2, c565(pDim()));
    }
  }
  label(TR("Mittagspause", "Lunch break"), R_LUNCH.cy());
  toggle(R_LUNCH, lunchOn);
  drawStepper(ST_LFROM, lunchFrom, lunchOn);
  txt(TR("bis", "to"), ux(706), ST_LFROM.value.cy(), 26, lunchOn ? pInk() : pDim(), pBg());
  drawStepper(ST_LTO, lunchTo, lunchOn);
  txt(TR("Besucher buchen nur in diesem Zeitfenster und an diesen Tagen.",
         "Visitors can only book within this time window and on these days."), SCR_W / 2, uy(462), 20, pDim(), pBg());
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
}

// ------------------------------------------------------------ Einrichtungs-WLAN
static void kv(const char* k, const char* v, int y) {
  txt(k, ux(60), y, 26, pDim(), pBg(), middle_left);
  txt(v, ux(380), y, 26, pInk(), pBg(), middle_left);
}

static void drawWifi() {
  header(TR("Einrichtungs-WLAN", "Setup Wi-Fi"));
  kv(TR("Zustand", "Status"), apOff ? TR("ausgeschaltet", "off")
                            : (webApState() == 1 ? TR("eingeschaltet", "on") : TR("Start fehlgeschlagen", "failed to start")), uy(120));
  kv(TR("Netz", "Network"), webSsid(), uy(175));
  kv(TR("Passwort", "Password"), apPassLoad().c_str(), uy(230));
  kv(TR("Webinterface", "Web interface"), "http://192.168.4.1", uy(285));
  txt(TR("Ohne Einrichtungs-WLAN geht alles auch hier im Men\xC3\xBC.", "Without setup Wi-Fi, everything can also be done here in the menu."),
      SCR_W / 2, uy(420), 20, pDim(), pBg());
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
  button(R_RIGHT, apOff ? TR("Einschalten", "Turn on") : TR("Ausschalten", "Turn off"), B_PRIMARY);
}

// ------------------------------------------------------------ Microsoft 365
static const char* o365Msg = "";

static void drawO365() {
  header("Microsoft 365");
  kv(TR("Konto", "Account"), account[0] ? account : TR("keins angemeldet", "none signed in"), uy(120));
  kv(TR("Kalender schreiben", "Write calendar"), canWrite ? TR("ja (Buchung m\xC3\xB6glich)", "yes (booking possible)") : TR("nein", "no"), uy(175));
  kv(TR("Verbindung", "Connection"), linked ? TR("Agent verbunden", "agent connected") : TR("kein Agent", "no agent"), uy(230));
  txt(TR("Die Anmeldung l\xC3\xA4uft \xC3\xBC" "ber den Agenten, Code und Adresse erscheinen hier.",
         "Sign-in runs through the agent, code and address appear here."),
      SCR_W / 2, uy(320), 20, pDim(), pBg());
  if (*o365Msg) txt(o365Msg, SCR_W / 2, uy(370), 26, pInk(), pBg());
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
  button(R_RIGHT, TR("Neues Konto verbinden", "Connect new account"), !linked ? B_DISABLED : B_PRIMARY, 20);
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
  kv(TR("Verbindung", "Connection"), demoMode ? TR("Demomodus", "demo mode")
                                     : linked ? TR("Agent verbunden", "agent connected") : TR("kein Agent", "no agent"), y); y += step;
  kv(TR("Konto", "Account"), account[0] ? account : TR("keins angemeldet", "none signed in"), y); y += step;
  kv(TR("Kalender schreiben", "Write calendar"), canWrite ? TR("ja (Buchung m\xC3\xB6glich)", "yes (booking possible)") : TR("nein", "no"), y); y += step;
  char b[64];                                   // stand frueher unten auf der Statusanzeige
  if (apOff) snprintf(b, sizeof(b), TR("%s, aus", "%s, off"), webSsid());
  else snprintf(b, sizeof(b), TR("%s, ein, http://192.168.4.1", "%s, on, http://192.168.4.1"), webSsid());
  kv(TR("Einrichtungs-WLAN", "Setup Wi-Fi"), b, y); y += step;
  kv(TR("Echtzeituhr", "Real-time clock"), rtcState == 1 ? TR("l\xC3\xA4uft", "running")
                                         : rtcState == 2 ? TR("Zeit verloren (Knopfzelle?)", "time lost (coin cell?)")
                                                         : TR("nicht gefunden", "not found"), y); y += step;
  kv(TR("Letzter Neustart", "Last restart"), resetReasonText(), y); y += step;
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
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
    case M_LANG:    drawLang(); break;
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
      pinMsg = TR("PIN falsch", "Wrong PIN");
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
        txt(TR("PIN ge\xC3\xA4ndert", "PIN changed"), SCR_W / 2, uy(80), 20, pInk(), pBg());
        return;
      }
      pinStep = P_NEW1;
      pinMsg = TR("Die beiden PIN stimmen nicht \xC3\xBC" "berein. Bitte neu eingeben.",
                  "The two PINs do not match. Please enter again.");
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
      case 8: go(M_LANG); return;
    }
  }
}

static void tapLang(int x, int y) {
  if (R_BACK.in(x, y)) { go(M_MAIN); return; }
  int l = R_LANG_DE.in(x, y) ? 0 : R_LANG_EN.in(x, y) ? 1 : -1;
  if (l < 0) return;
  if (l != uiLang) langSet((uint8_t)l);          // speichert, meldet dem Agenten
  drawLang();                                    // gleich in der neuen Sprache
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
    textEntryOpen(TR("Name in der Kopfzeile", "Name in the header"), nameWork, 41, onNameDone);   // 40 Zeichen wie im Webinterface
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
  if (!linked) { o365Msg = TR("Kein Agent verbunden.", "No agent connected."); drawO365(); return; }
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
    case M_LANG:    tapLang(x, y); break;
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
