/******************************************************************************
 * booking.cpp  -  Terminbuchung am Display (fuer Besucher, wenn man nicht da ist)
 *
 * Ablauf: E-Mail -> Titel (je Bildschirmtastatur) -> Dauer 15/30/60 Min ->
 * Tag (laufende und kommende Woche) -> freie Uhrzeit -> bestaetigen.
 * Die freien Zeiten berechnet der Agent aus dem Kalender (inkl. Mittagspause),
 * er legt auch den Termin mit Einladung an:
 *   Geraet: SLOTS <dauer> <frueheste> <spaeteste> <tage_maske> <mittag_von> <mittag_bis>
 *           (Zeiten in Minuten ab 00:00, Mittag 0 0 = keine Pause)
 *   Agent : SL <yyyy-mm-dd> <wochentag> <HHMM,...|->  ...  SL END | SL ERR <text>
 *   Geraet: BOOK <yyyy-mm-dd> <HH:MM> <dauer> <email> <titel ...>
 *   Agent : BK OK | BK ERR <text>
 *
 * Gestaltung in den Farben der linken Kachel, kantengeglaettete Schrift
 * (Montserrat, echte Umlaute). 90 s ohne Beruehrung -> zurueck zur
 * Statusanzeige. Hoechstens 4 Buchungen pro Stunde.
 *
 * Demomodus: freie Zeiten werden erfunden, Buchen wird nur simuliert (kein
 * Agent, kein Termin). bookingDemoStart() tippt eine Buchung selbst durch.
 ******************************************************************************/
#include "widgets.h"

enum Screen { S_OFF, S_EMAIL, S_TITLE, S_TEXT, S_DUR, S_LOAD, S_DAY, S_TIME, S_CONFIRM, S_SEND, S_RESULT };

struct Day { char date[11]; uint8_t wd; uint8_t n; uint16_t slot[64]; };   // slot = Minuten ab 00:00

static Screen scr = S_OFF;
static char email[64];
static char title[96];
static int dur = 30;
static Day days[14];
static int nDays = 0, selDay = -1, selSlot = -1, page = 0;
static bool resultOk = false, shift = false;
static char resultMsg[96];
static uint32_t lastTouch = 0, since = 0;
static uint32_t bookedAt[4] = { 0, 0, 0, 0 };
static bool script = false;                      // Demo: Buchung wird selbst durchgetippt
// Allgemeine Texteingabe (S_TEXT): Arbeitskopie, Ziel und Rueckruf
static char textWork[64];
static char* textBuf = nullptr;
static size_t textMax = 0;
static const char* textTitle = "";
static void (*textDone)(bool ok) = nullptr;

static const char* WD_DE[7] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };
static const char* WD_EN[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
static const char* wdName(int i) { return (uiLang ? WD_EN : WD_DE)[i % 7]; }

static void iconBackspace(const Rect& r, Style st) {
  button(r, nullptr, st);
  uint16_t c = c565(btnFg(st)), b = c565(btnBg(st));
  int cx = r.cx(), cy = r.cy();
  lcd.fillTriangle(cx - 26, cy, cx - 10, cy - 15, cx - 10, cy + 15, c);
  lcd.fillRoundRect(cx - 12, cy - 15, 38, 30, 4, c);
  lcd.drawWideLine(cx - 2, cy - 7, cx + 14, cy + 7, 2, b);
  lcd.drawWideLine(cx - 2, cy + 7, cx + 14, cy - 7, 2, b);
}
static void iconShift(const Rect& r, Style st) {
  button(r, nullptr, st);
  uint16_t c = c565(btnFg(st));
  int cx = r.cx(), cy = r.cy();
  lcd.fillTriangle(cx, cy - 18, cx - 18, cy + 2, cx + 18, cy + 2, c);
  lcd.fillRect(cx - 8, cy + 2, 16, 14, c);
}

static void fmtDate(const Day& d, char* out, size_t n) {    // "Mo 20.10."
  snprintf(out, n, "%s %.2s.%.2s.", wdName(d.wd), d.date + 8, d.date + 5);
}
static void fmtTime(int m, char* out, size_t n) { snprintf(out, n, "%02d:%02d", m / 60, m % 60); }

// ------------------------------------------------------------ Tastatur
// Tasten als UTF-8-Texte, "\x01" = Umschalttaste. Deutsch QWERTZ mit Umlauten, Englisch QWERTY ohne.
static const char* KB_EMAIL[4][14] = {
  { "1","2","3","4","5","6","7","8","9","0", nullptr },
  { "q","w","e","r","t","z","u","i","o","p", nullptr },
  { "a","s","d","f","g","h","j","k","l", nullptr },
  { "y","x","c","v","b","n","m",".","-","_", nullptr },
};
static const char* KB_TITLE[4][14] = {
  { "1","2","3","4","5","6","7","8","9","0","?","!", nullptr },
  { "q","w","e","r","t","z","u","i","o","p","\xC3\xBC", nullptr },
  { "a","s","d","f","g","h","j","k","l","\xC3\xB6","\xC3\xA4", nullptr },
  { "\x01","y","x","c","v","b","n","m",",",".","-", nullptr },
};
static const char* KB_EMAIL_EN[4][14] = {
  { "1","2","3","4","5","6","7","8","9","0", nullptr },
  { "q","w","e","r","t","y","u","i","o","p", nullptr },
  { "a","s","d","f","g","h","j","k","l", nullptr },
  { "z","x","c","v","b","n","m",".","-","_", nullptr },
};
static const char* KB_TITLE_EN[4][14] = {
  { "1","2","3","4","5","6","7","8","9","0","?","!", nullptr },
  { "q","w","e","r","t","y","u","i","o","p", nullptr },
  { "a","s","d","f","g","h","j","k","l","'", nullptr },
  { "\x01","z","x","c","v","b","n","m",",",".","-", nullptr },
};
static const int KB_Y0 = ly(146), KB_KH = ly(56), KB_ROW = ly(62), KB_GAP = 6;
static const Rect R_FIELD = { lx(20), ly(64), lx(760), ly(66) };
// Unterste Reihe E-Mail
static const Rect R_AT = { lx(20), ly(398), lx(96), ly(68) }, R_CH = { lx(124), ly(398), lx(96), ly(68) }, R_COM = { lx(228), ly(398), lx(110), ly(68) };
static const Rect R_DEL_E = { lx(346), ly(398), lx(96), ly(68) }, R_CANCEL = { lx(450), ly(398), lx(150), ly(68) };
static const Rect R_NEXT = { lx(608), ly(398), lx(172), ly(68) };
// Unterste Reihe Titel
static const Rect R_SPACE = { lx(20), ly(398), lx(318), ly(68) }, R_DEL_T = { lx(346), ly(398), lx(96), ly(68) }, R_BACK_T = { lx(450), ly(398), lx(150), ly(68) };

typedef const char* KbRow[14];
static const KbRow* kbRows() {
  if (uiLang) return scr == S_EMAIL ? KB_EMAIL_EN : KB_TITLE_EN;
  return scr == S_EMAIL ? KB_EMAIL : KB_TITLE;
}
static int rowLen(int r) { int n = 0; while (kbRows()[r][n]) n++; return n; }

static Rect keyRect(int r, int i) {
  int n = rowLen(r);
  int w = (lx(760) - (n - 1) * KB_GAP) / n; if (w > lx(72)) w = lx(72);
  int x0 = (SCR_W - (n * w + (n - 1) * KB_GAP)) / 2;
  return { x0 + i * (w + KB_GAP), KB_Y0 + r * KB_ROW, w, KB_KH };
}

// Grossbuchstabe zu einer Taste (ASCII und Umlaute)
static void keyText(const char* k, bool upper, char* out) {
  strcpy(out, k);
  if (!upper) return;
  if ((uint8_t)k[0] == 0xC3) {                   // ae oe ue -> AE OE UE
    if ((uint8_t)k[1] == 0xA4) out[1] = (char)0x84;
    if ((uint8_t)k[1] == 0xB6) out[1] = (char)0x96;
    if ((uint8_t)k[1] == 0xBC) out[1] = (char)0x9C;
  } else out[0] = toupper(k[0]);
}

static char* buf() { return scr == S_TEXT ? textWork : scr == S_TITLE ? title : email; }
static size_t bufSize() { return scr == S_TEXT ? textMax : scr == S_TITLE ? sizeof(title) : sizeof(email); }

static bool emailValid() {
  const char* at = strchr(email, '@');
  if (!at || at == email || strchr(at + 1, '@')) return false;
  const char* dot = strrchr(at, '.');
  return dot && dot > at + 1 && strlen(dot) >= 3;
}
static bool titleValid() {
  int n = 0;
  for (const char* p = title; *p; p++) if (*p != ' ') n++;
  return n >= 2;
}
static bool inputValid() { return scr == S_TEXT ? true : scr == S_TITLE ? titleValid() : emailValid(); }

static void drawField() {
  uint32_t fb = pField();
  lcd.fillRoundRect(R_FIELD.x, R_FIELD.y, R_FIELD.w, R_FIELD.h, 12, c565(fb));
  const char* s = buf();
  int y = R_FIELD.cy();
  if (!*s) {
    txt(scr == S_TEXT ? TR("leer = Konto vom Agent", "empty = account from agent")
        : scr == S_TITLE ? TR("Worum geht es?", "What is it about?") : TR("name@firma.ch", "name@company.com"),
        R_FIELD.x + 20, y, 26, pDim(), fb, middle_left);
  } else {
    lcd.setFont(smooth(34));
    const char* shown = s;                       // lange Eingaben: Ende zeigen
    while (*shown && lcd.textWidth(shown) > R_FIELD.w - 50) {
      shown++;
      while (((uint8_t)*shown & 0xC0) == 0x80) shown++;   // nicht mitten im UTF-8-Zeichen
    }
    txt(shown, R_FIELD.x + 20, y, 34, pInk(), fb, middle_left);
    lcd.setFont(smooth(34));
    int cx = R_FIELD.x + 22 + lcd.textWidth(shown);
    lcd.fillRect(cx, R_FIELD.y + 14, 3, R_FIELD.h - 28, c565(pInk()));   // Cursor
  }
  button(R_NEXT, scr == S_TEXT ? TR("Fertig", "Done") : TR("Weiter", "Next"), inputValid() ? B_PRIMARY : B_DISABLED, 20);
}

static void drawKeys() {
  bool upper = scr != S_EMAIL && shift;
  for (int r = 0; r < 4; r++)
    for (int i = 0; kbRows()[r][i]; i++) {
      const char* k = kbRows()[r][i];
      if (k[0] == '\x01') { iconShift(keyRect(r, i), shift ? B_PRIMARY : B_NORMAL); continue; }
      char t[4]; keyText(k, upper, t);
      button(keyRect(r, i), t, B_NORMAL, 26);
    }
}

static void drawKeyboardScreen() {
  if (scr == S_TEXT) {
    header(textTitle);
    button(R_SPACE, TR("Leerzeichen", "Space"), B_NORMAL, 20);
    iconBackspace(R_DEL_T, B_NORMAL);
    button(R_BACK_T, TR("Abbrechen", "Cancel"), B_NORMAL, 20);
  } else if (scr == S_TITLE) {
    header(TR("Titel des Termins", "Meeting title"));
    button(R_SPACE, TR("Leerzeichen", "Space"), B_NORMAL, 20);
    iconBackspace(R_DEL_T, B_NORMAL);
    button(R_BACK_T, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL, 20);
  } else {
    header(TR("Ihre E-Mail-Adresse", "Your e-mail address"));
    button(R_AT, "@", B_NORMAL, 26);
    button(R_CH, ".ch", B_NORMAL, 20);
    button(R_COM, ".com", B_NORMAL, 20);
    iconBackspace(R_DEL_E, B_NORMAL);
    button(R_CANCEL, TR("Abbrechen", "Cancel"), B_NORMAL, 20);
  }
  drawKeys();
  drawField();
}

static void append(const char* s) {
  char* b = buf();
  size_t l = strlen(b);
  if (l + strlen(s) < bufSize() - 1) strcat(b, s);
  if (scr != S_EMAIL && shift) { shift = false; drawKeys(); }   // Umschalten gilt fuer ein Zeichen
  drawField();
}

static void backspace() {
  char* b = buf();
  size_t l = strlen(b);
  while (l && ((uint8_t)b[l - 1] & 0xC0) == 0x80) l--;           // UTF-8-Folgebytes
  if (l) l--;
  b[l] = '\0';
  if (scr != S_EMAIL && l == 0 && !shift) { shift = true; drawKeys(); }
  drawField();
}

// ------------------------------------------------------------ Dauer
static const Rect R_D[3] = { { lx(40), ly(150), lx(220), ly(150) }, { lx(290), ly(150), lx(220), ly(150) }, { lx(540), ly(150), lx(220), ly(150) } };
static const int DURS[3] = { 15, 30, 60 };
static const Rect R_BACK = { lx(20), ly(398), lx(200), ly(68) };
static const Rect R_GO   = { lx(560), ly(398), lx(220), ly(68) };

static void drawDur() {
  header(TR("Wie lange soll der Termin dauern?", "How long should the meeting be?"));
  const char* l[3] = { TR("15 Min", "15 min"), TR("30 Min", "30 min"), TR("60 Min", "60 min") };
  for (int i = 0; i < 3; i++) button(R_D[i], l[i], B_NORMAL, 48);
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
}

// ------------------------------------------------------------ Meldungen
static void drawMessage(const char* t, const char* line) {
  header(t);
  txt(line, SCR_W / 2, ly(220), 26, pDim(), pBg());
}

static void drawResult() {
  header(resultOk ? TR("Termin gebucht", "Meeting booked") : TR("Das hat nicht geklappt", "That did not work"));
  if (resultOk) {
    txt(TR("Die Einladung geht an", "The invitation goes to"), SCR_W / 2, ly(180), 26, pDim(), pBg());
    txt(email, SCR_W / 2, ly(230), 34, pInk(), pBg());
  } else {
    txt(resultMsg, SCR_W / 2, ly(210), 26, pInk(), pBg());
  }
  button(R_GO, "OK", B_PRIMARY);
}

// ------------------------------------------------------------ Tag
static Rect dayRect(int idx) {
  // erste Woche oben, zweite unten; Spalten je nach Anzahl Tage pro Woche
  int perWeek = 0;
  for (int b = 0; b < 7; b++) if (bookDays & (1 << b)) perWeek++;
  if (perWeek == 0) perWeek = 1;
  int row = idx / perWeek, col = idx % perWeek;
  int w = (lx(760) - (perWeek - 1) * 8) / perWeek;
  return { lx(20) + col * (w + 8), ly(row ? 262 : 104), w, ly(116) };
}

static void drawDay() {
  char t1[48]; snprintf(t1, sizeof(t1), TR("Tag w\xC3\xA4hlen (%d Min)", "Choose a day (%d min)"), dur);
  header(t1);
  txt(TR("Diese Woche", "This week"), lx(20), ly(88), 20, pDim(), pBg(), middle_left);
  txt(TR("N\xC3\xA4" "chste Woche", "Next week"), lx(20), ly(246), 20, pDim(), pBg(), middle_left);
  for (int i = 0; i < nDays; i++) {
    Rect r = dayRect(i);
    Style st = days[i].n ? B_NORMAL : B_DISABLED;
    button(r, nullptr, st);
    char d[16]; fmtDate(days[i], d, sizeof(d));
    txt(d, r.cx(), r.y + ly(42), 26, btnFg(st), btnBg(st));
    char n[16];
    if (days[i].n) snprintf(n, sizeof(n), TR("%d frei", "%d free"), days[i].n); else strcpy(n, TR("belegt", "booked"));
    txt(n, r.cx(), r.y + ly(82), 20, st == B_DISABLED ? pDim() : mixRGB(pInk(), pBtn(), 30), btnBg(st));
  }
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
}

// ------------------------------------------------------------ Uhrzeit
static const int TCOLS = 6, TROWS = 5, TPER = TCOLS * TROWS;
static const Rect R_PREV = { lx(300), ly(398), lx(200), ly(68) };
static const Rect R_NEXTP = { lx(520), ly(398), lx(260), ly(68) };

static Rect timeRect(int k) {
  int c = k % TCOLS, r = k / TCOLS;
  return { lx(20 + c * 128), ly(74 + r * 64), lx(120), ly(56) };
}

static void drawTime() {
  const Day& d = days[selDay];
  char t1[48], ds[16]; fmtDate(d, ds, sizeof(ds));
  snprintf(t1, sizeof(t1), TR("%s, Beginn w\xC3\xA4hlen", "%s, choose start time"), ds);
  header(t1);
  int first = page * TPER;
  for (int k = 0; k < TPER && first + k < d.n; k++) {
    char tm[8]; fmtTime(d.slot[first + k], tm, sizeof(tm));
    button(timeRect(k), tm, B_NORMAL);
  }
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
  if (page > 0) button(R_PREV, TR("fr\xC3\xBC" "her", "earlier"), B_NORMAL);
  if (first + TPER < d.n) button(R_NEXTP, TR("sp\xC3\xA4ter", "later"), B_NORMAL);
}

// ------------------------------------------------------------ Bestaetigen
static void drawConfirm() {
  header(TR("Termin best\xC3\xA4tigen", "Confirm meeting"));
  const Day& d = days[selDay];
  int m = d.slot[selSlot];
  char ds[16], a[8], b[8], line[64];
  fmtDate(d, ds, sizeof(ds)); fmtTime(m, a, sizeof(a)); fmtTime(m + dur, b, sizeof(b));
  snprintf(line, sizeof(line), TR("%s  %s bis %s", "%s  %s to %s"), ds, a, b);
  txt(title, SCR_W / 2, ly(110), 34, pInk(), pBg());
  txt(line, SCR_W / 2, ly(170), 34, pInk(), pBg());
  txt(TR("mit", "with"), SCR_W / 2, ly(222), 20, pDim(), pBg());
  txt(email, SCR_W / 2, ly(262), 26, pInk(), pBg());
  txt(TR("Sie erhalten eine Einladung per E-Mail.", "You will receive an invitation by e-mail."), SCR_W / 2, ly(330), 20, pDim(), pBg());
  button(R_BACK, TR("Zur\xC3\xBC" "ck", "Back"), B_NORMAL);
  button(R_GO, TR("Buchen", "Book"), B_PRIMARY);
}

// ------------------------------------------------------------ Demo: erfundene Zeiten
static void demoSlots() {
  struct tm now; localNow(now);
  int wdNow = (now.tm_wday + 6) % 7;                           // 0 = Montag
  int nowMin = now.tm_hour * 60 + now.tm_min + 10;
  time_t base = localEpoch() - wdNow * 86400;                 // gleiche Uhrzeit am Montag
  int step = dur <= 15 ? 15 : 30;
  nDays = 0;
  for (int i = 0; i < 14 && nDays < 14; i++) {
    if (!(bookDays & (1 << (i % 7)))) continue;
    time_t dayT = base + i * 86400;
    struct tm d; gmtime_r(&dayT, &d);
    Day& D = days[nDays++];
    strftime(D.date, sizeof(D.date), "%Y-%m-%d", &d);
    const char* fake = shotDateText();                         // Bildschirmfotos: erfundene Tage
    int fd = 0, fm = 0, fy = 0;                                // um das Demo-Datum herum
    if (*fake && sscanf(fake, "%d.%d.%d", &fd, &fm, &fy) == 3) {
      int n = fd - wdNow + i;
      if (n > 30) { n -= 30; fm++; }
      snprintf(D.date, sizeof(D.date), "%04d-%02d-%02d", fy, fm, n);
    }
    D.wd = i % 7;
    D.n = 0;
    if (i < wdNow) continue;                                   // vergangene Tage
    for (int m = bookFrom; m + dur <= bookTo && D.n < 64; m += step) {
      if (i == wdNow && m < nowMin) continue;
      if (lunchOn && m < lunchTo && m + dur > lunchFrom) continue;
      if (((i * 7 + m / 30 * 3) % 5) == 0) continue;           // ein paar Zeiten "belegt"
      D.slot[D.n++] = m;
    }
  }
}

// ------------------------------------------------------------ Ablauf
static void go(Screen s) {
  scr = s;
  since = millis();
  switch (s) {
    case S_EMAIL:
    case S_TITLE:
    case S_TEXT:    drawKeyboardScreen(); break;
    case S_DUR:     drawDur(); break;
    case S_LOAD:    drawMessage(TR("Freie Zeiten", "Free times"), TR("werden geladen ...", "loading ...")); break;
    case S_DAY:     drawDay(); break;
    case S_TIME:    drawTime(); break;
    case S_CONFIRM: drawConfirm(); break;
    case S_SEND:    drawMessage(TR("Termin wird gebucht", "Booking the meeting"), TR("einen Moment ...", "one moment ...")); break;
    case S_RESULT:  drawResult(); break;
    default:        uiVersion++; break;          // Statusanzeige neu zeichnen
  }
}

static void fail(const char* msg) {
  resultOk = false;
  strncpy(resultMsg, msg, sizeof(resultMsg) - 1);
  resultMsg[sizeof(resultMsg) - 1] = '\0';
  for (char* p = resultMsg; *p; p++) if (*p == '_') *p = ' ';
  go(S_RESULT);
}

static int bookingsLastHour() {
  int n = 0;
  for (uint32_t t : bookedAt) if (t && millis() - t < 3600000UL) n++;
  return n;
}

bool bookingActive() { return scr != S_OFF; }

void textEntryOpen(const char* t, char* b, size_t max, void (*done)(bool ok)) {
  if (max > sizeof(textWork)) max = sizeof(textWork);
  textTitle = t; textBuf = b; textMax = max; textDone = done;
  strncpy(textWork, b, max - 1); textWork[max - 1] = '\0';
  shift = !textWork[0];
  lastTouch = millis();
  go(S_TEXT);
}
void bookingCancel() { if (scr != S_OFF && scr != S_TEXT) go(S_OFF); }   // Namenseingabe braucht keinen Agent

void bookingOpen() {
  email[0] = title[0] = '\0';
  lastTouch = millis();
  if (demoMode) { go(S_EMAIL); return; }
  if (!linked) { fail(TR("Gerade nicht m\xC3\xB6glich, der Computer ist nicht verbunden.", "Not possible right now, the computer is not connected.")); return; }
  if (bookingsLastHour() >= 4) { fail(TR("Zu viele Buchungen. Bitte sp\xC3\xA4ter nochmals versuchen.", "Too many bookings. Please try again later.")); return; }
  go(S_EMAIL);
}

// Gedrueckte Taste kurz hervorheben (Rueckmeldung beim Tippen), bookingLoop setzt sie zurueck
static int pressR = -1, pressI = -1;
static uint32_t pressAt = 0;
static void keyUp() {
  if (pressR < 0) return;
  int r = pressR, i = pressI;
  pressR = pressI = -1;
  if (scr != S_EMAIL && scr != S_TITLE && scr != S_TEXT) return;
  if (i >= rowLen(r)) return;
  const char* k = kbRows()[r][i];
  if (k[0] == '\x01') return;                    // Umschalttaste zeichnet sich selbst
  char t[4]; keyText(k, scr != S_EMAIL && shift, t);
  button(keyRect(r, i), t, B_NORMAL, 26);
}

static bool tapKeys(int x, int y) {
  keyUp();
  for (int r = 0; r < 4; r++)
    for (int i = 0; kbRows()[r][i]; i++) {
      if (!keyRect(r, i).in(x, y)) continue;
      const char* k = kbRows()[r][i];
      if (k[0] != '\x01') {
        char t[4]; keyText(k, scr != S_EMAIL && shift, t);
        button(keyRect(r, i), t, B_PRIMARY, 26);
        pressR = r; pressI = i; pressAt = millis();
      }
      if (k[0] == '\x01') { shift = !shift; drawKeys(); return true; }
      char t[4]; keyText(k, scr != S_EMAIL && shift, t);
      append(t);
      return true;
    }
  return false;
}

void bookingTap(int x, int y) {
  lastTouch = millis();
  script = false;                                // Besucher uebernimmt die Vorfuehrung
  switch (scr) {
    case S_EMAIL:
      if (tapKeys(x, y)) return;
      if (R_AT.in(x, y))     { append("@"); return; }
      if (R_CH.in(x, y))     { append(".ch"); return; }
      if (R_COM.in(x, y))    { append(".com"); return; }
      if (R_DEL_E.in(x, y))  { backspace(); return; }
      if (R_CANCEL.in(x, y)) { go(S_OFF); return; }
      if (R_NEXT.in(x, y) && emailValid()) { shift = !title[0]; go(S_TITLE); }
      return;
    case S_TEXT: {
      if (tapKeys(x, y)) return;
      if (R_SPACE.in(x, y))  { if (textWork[0]) append(" "); return; }
      if (R_DEL_T.in(x, y))  { backspace(); return; }
      bool ok = R_NEXT.in(x, y), cancel = R_BACK_T.in(x, y);
      if (!ok && !cancel) return;
      if (ok && textBuf) {
        size_t l = strlen(textWork);
        while (l && textWork[l - 1] == ' ') textWork[--l] = '\0';   // Leerzeichen am Ende weg
        strncpy(textBuf, textWork, textMax - 1); textBuf[textMax - 1] = '\0';
      }
      void (*done)(bool) = textDone;
      textBuf = nullptr; textDone = nullptr;
      go(S_OFF);
      if (done) done(ok);
      return;
    }
    case S_TITLE:
      if (tapKeys(x, y)) return;
      if (R_SPACE.in(x, y))  { if (title[0]) append(" "); return; }
      if (R_DEL_T.in(x, y))  { backspace(); return; }
      if (R_BACK_T.in(x, y)) { go(S_EMAIL); return; }
      if (R_NEXT.in(x, y) && titleValid()) go(S_DUR);
      return;
    case S_DUR:
      for (int i = 0; i < 3; i++)
        if (R_D[i].in(x, y)) {
          dur = DURS[i];
          nDays = 0;
          if (demoMode) { demoSlots(); go(S_LOAD); return; }    // S_LOAD geht nach kurzer Pause weiter
          char cmd[64];
          snprintf(cmd, sizeof(cmd), "SLOTS %d %u %u %u %u %u", dur, bookFrom, bookTo, bookDays,
                   lunchOn ? lunchFrom : 0, lunchOn ? lunchTo : 0);
          sendCmd(cmd);
          go(S_LOAD);
          return;
        }
      if (R_BACK.in(x, y)) go(S_TITLE);
      return;
    case S_DAY:
      for (int i = 0; i < nDays; i++)
        if (days[i].n && dayRect(i).in(x, y)) { selDay = i; page = 0; go(S_TIME); return; }
      if (R_BACK.in(x, y)) go(S_DUR);
      return;
    case S_TIME: {
      int first = page * TPER;
      for (int k = 0; k < TPER && first + k < days[selDay].n; k++)
        if (timeRect(k).in(x, y)) { selSlot = first + k; go(S_CONFIRM); return; }
      if (R_BACK.in(x, y)) { go(S_DAY); return; }
      if (page > 0 && R_PREV.in(x, y)) { page--; drawTime(); return; }
      if (first + TPER < days[selDay].n && R_NEXTP.in(x, y)) { page++; drawTime(); }
      return;
    }
    case S_CONFIRM:
      if (R_BACK.in(x, y)) { go(S_TIME); return; }
      if (R_GO.in(x, y)) {
        if (demoMode) { go(S_SEND); return; }                  // simuliert, kein echter Termin
        int m = days[selDay].slot[selSlot];
        char cmd[200];
        snprintf(cmd, sizeof(cmd), "BOOK %s %02d:%02d %d %s %s", days[selDay].date, m / 60, m % 60, dur, email, title);
        sendCmd(cmd);
        go(S_SEND);
      }
      return;
    case S_RESULT:
      if (R_GO.in(x, y)) go(S_OFF);
      return;
    default:
      return;
  }
}

// ------------------------------------------------------------ Demo: Vorfuehrung
static int sStep = 0;
static uint32_t sNext = 0;
static const char* S_EMAIL_TXT = "anna.muster@firma.ch";
static const char* S_TITLE_TXT = "Projektbesprechung";
static const char* S_TITLE_TXT_EN = "Project meeting";

void bookingDemoStart() {
  email[0] = title[0] = '\0';
  script = true;
  sStep = 0;
  sNext = millis() + 900;
  lastTouch = millis();
  go(S_EMAIL);
}

// naechstes Zeichen (UTF-8) aus src anhaengen, false wenn fertig
static bool typeNext(const char* src) {
  size_t l = strlen(buf());
  if (!src[l]) return false;
  size_t n = 1;
  while (((uint8_t)src[l + n] & 0xC0) == 0x80) n++;
  char c[5] = {}; memcpy(c, src + l, n);
  append(c);
  return true;
}

static void scriptTick() {
  uint32_t now = millis();
  if (now < sNext) return;
  lastTouch = now;
  switch (sStep) {
    case 0: if (typeNext(S_EMAIL_TXT)) { sNext = now + 110; return; } sNext = now + 1000; break;
    case 1: shift = true; go(S_TITLE); sNext = now + 700; break;
    case 2: if (typeNext(TR(S_TITLE_TXT, S_TITLE_TXT_EN))) { sNext = now + 120; return; } sNext = now + 1000; break;
    case 3: go(S_DUR); sNext = now + 1800; break;
    case 4: dur = 30; demoSlots(); go(S_LOAD); sNext = now + 1000; break;
    case 5: go(S_DAY); sNext = now + 2200; break;
    case 6: selDay = 0;
            while (selDay < nDays - 1 && !days[selDay].n) selDay++;
            if (!days[selDay].n) { script = false; go(S_OFF); return; }
            page = 0; go(S_TIME); sNext = now + 2200; break;
    case 7: selSlot = days[selDay].n > 2 ? 2 : 0; go(S_CONFIRM); sNext = now + 3200; break;
    case 8: go(S_SEND); sNext = now + 1500; break;
    case 9: resultOk = true; go(S_RESULT); sNext = now + 4500; break;
    default: script = false; go(S_OFF); return;
  }
  sStep++;
}

void bookingLoop() {
  if (scr == S_OFF) return;
  uint32_t now = millis();
  if (pressR >= 0 && now - pressAt > 120) keyUp();
  if (script) { scriptTick(); return; }
  if (demoMode && scr == S_LOAD && now - since > 900) { go(S_DAY); return; }
  if (demoMode && scr == S_SEND && now - since > 1500) { resultOk = true; go(S_RESULT); return; }
  if ((scr == S_LOAD || scr == S_SEND) && now - since > 30000) { fail(TR("Keine Antwort vom Computer.", "No response from the computer.")); return; }
  if (scr == S_RESULT && resultOk && now - since > 15000) { go(S_OFF); return; }
  if (scr != S_LOAD && scr != S_SEND && now - lastTouch > 90000) go(S_OFF);
}

bool bookingSerial(char* line) {
  if (!strncmp(line, "SL ", 3)) {
    if (scr != S_LOAD) return true;               // verspaetete Antwort ignorieren
    if (!strncmp(line + 3, "END", 3)) { go(S_DAY); return true; }
    if (!strncmp(line + 3, "ERR", 3)) { fail(line[6] ? line + 7 : TR("Fehler", "Error")); return true; }
    if (nDays >= 14) return true;
    Day& d = days[nDays];
    char date[11] = "", list[400] = "";
    int wd = 0;
    if (sscanf(line + 3, "%10s %d %399s", date, &wd, list) < 2) return true;
    strcpy(d.date, date);
    d.wd = wd;
    d.n = 0;
    if (strcmp(list, "-")) {
      for (char* p = strtok(list, ","); p && d.n < 64; p = strtok(nullptr, ",")) {
        int v = atoi(p);
        d.slot[d.n++] = (v / 100) * 60 + v % 100;
      }
    }
    nDays++;
    return true;
  }
  if (!strncmp(line, "BK ", 3)) {
    if (scr != S_SEND) return true;
    if (!strncmp(line + 3, "OK", 2)) {
      for (auto& t : bookedAt) if (!t || millis() - t >= 3600000UL) { t = millis(); break; }
      resultOk = true;
      go(S_RESULT);
    } else fail(line[6] ? line + 7 : TR("Fehler", "Error"));
    return true;
  }
  return false;
}
