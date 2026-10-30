/******************************************************************************
 * Tally32  -  Elecrow CrowPanel ESP32 HMI 7.0" (quer 800x480)
 *
 * Statusanzeige fuer den Arbeitsplatz. Der Agent auf dem Computer
 * (agent/tally_agent.py) ermittelt den Status und schickt ihn per USB-Seriell:
 *
 *   HELLO                                  -> Antwort "TALLY32 1"
 *   ST <mode> <uhr> <frei_ab> <frei_bis> <quelle>
 *        mode     : call | busy | away | free
 *        uhr      : HH:MM (Geraet hat keine RTC, Uhrzeit kommt vom Host)
 *        frei_ab  : HH:MM oder TT.MM. wieder frei, sonst "-"
 *                   (busy/away/call: Ende des laufenden Blocks,
 *                    free: Ende des naechsten Blocks = "danach wieder frei ab")
 *        frei_bis : HH:MM Beginn des naechsten Termins (nur free), sonst "-"
 *        quelle   : z.B. Teams, 3CX, Mikrofon, Kalender ("_" = Leerzeichen)
 *
 * Kommt laenger als LINK_TIMEOUT_MS nichts, zeigt das Display KEIN SIGNAL.
 * Layout: linke Spalte pastell dunkelgelb mit Uhr und Symbol, rechte Spalte
 * in der Statusfarbe (frei gruen, im Gespraech rot, besetzt orange,
 * abwesend dunkelblau). Jeder Zustand ist zusaetzlich ueber Text UND Symbol
 * unterscheidbar, nicht nur ueber die Farbe.
 *
 *   SET flip <0|1>                         -> Display 180 Grad drehen (NVS)
 *   SET bright <12..255>                   -> Helligkeit (NVS)
 *   AC <konto>                             -> angemeldetes Microsoft-Konto
 *   LC <code> <url> / LR OK <konto> / LR ERR <text>
 *                                          -> Office-365-Anmeldung (Geraet fragt mit "LOGIN" an)
 *
 * Konfiguration ueber WLAN-Access-Point und Webinterface (web.cpp).
 * 10 s auf das Display druecken -> Web- und WLAN-Passwort auf Standard.
 ******************************************************************************/
#include <Arduino.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include <esp_system.h>
#include "board.h"
#include "rtc.h"
#include "state.h"
#include "ui.h"
#include "logo_small_rgba.h"
#include "icons_conn.h"
#include "walker_icon.h"

// LovyanGFX legt beim Zeichnen jedes VLW-Zeichens das Glyphenbild auf den Stack (alloca).
// Grosse Ziffern brauchen viel davon (auf dem 10" lief der Standard von 8 KB am
// 30.10.2026 ueber), darum mehr Stack fuer die Hauptschleife.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

constexpr uint32_t LINK_TIMEOUT_MS = 20000;

Status cur = { M_OFFLINE, "", "", "", "", false, "" };
bool linked = false;
char account[64] = "";
bool canWrite = false;
uint32_t uiVersion = 0;

static Status shown = { M_NONE, "", "", "", "", false, "" };
static bool shownLinked = false;

// ----------------------------------------------------------------- Offline-Puffer
// Der Agent schickt alle 30 s Uhrzeit (TM) und Terminbloecke von heute und
// morgen (CB). Ist das Notebook weg, rechnet das Display damit selbst weiter.
// Damit das auch einen Neustart uebersteht (Dock nimmt beim Abdocken kurz den
// Strom weg), liegt der Puffer zusaetzlich im NVS und die Uhrzeit in der
// Echtzeituhr (rtc.h, braucht eine Knopfzelle).
struct CalBlock { uint16_t from, to; bool away; };          // Minuten ab 00:00
struct CalDay { char date[11]; uint8_t n; CalBlock b[16]; };
static CalDay calDays[2] = {};
static bool timeValid = false;
static int32_t tzOffset = 0;                    // Sekunden zu UTC
static time_t calSyncUtc = 0;                   // UTC beim letzten CB
static char lastStand[8] = "";                  // Uhrzeit der letzten Daten
static const time_t CACHE_MAX_S = 16 * 3600;
int rtcState = RTC_NONE;
static uint32_t rtcWrittenMs = 0;

struct OfflineCache {                           // NVS-Blob "cal"
  uint32_t magic;
  int32_t tz;
  int64_t sync;
  char stand[8];
  CalDay days[2];
};
static const uint32_t CACHE_MAGIC = 0x54333201;  // "T32" + Version

static void cacheLoad() {
  OfflineCache c;
  Preferences p; p.begin("tally", true);
  size_t n = p.getBytes("cal", &c, sizeof(c));
  p.end();
  if (n != sizeof(c) || c.magic != CACHE_MAGIC) return;
  tzOffset = c.tz;
  calSyncUtc = (time_t)c.sync;
  memcpy(lastStand, c.stand, sizeof(lastStand));
  lastStand[sizeof(lastStand) - 1] = '\0';
  memcpy(calDays, c.days, sizeof(calDays));
}

// Speichert bei geaenderten Terminen sofort, sonst hoechstens alle 5 min (Stand)
static void cacheSave() {
  static OfflineCache saved = {};
  static uint32_t savedMs = 0;
  OfflineCache c = {};
  c.magic = CACHE_MAGIC;
  c.tz = tzOffset;
  c.sync = calSyncUtc;
  memcpy(c.stand, lastStand, sizeof(c.stand));
  memcpy(c.days, calDays, sizeof(c.days));
  bool content = memcmp(c.days, saved.days, sizeof(c.days)) || c.tz != saved.tz || saved.magic != CACHE_MAGIC;
  if (!content && savedMs && millis() - savedMs < 5UL * 60 * 1000) return;
  if (!content && !memcmp(&c, &saved, sizeof(c))) return;
  Preferences p; p.begin("tally", false);
  p.putBytes("cal", &c, sizeof(c));
  p.end();
  saved = c;
  savedMs = millis();
}

const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "Strom ein";
    case ESP_RST_BROWNOUT: return "Spannungseinbruch";
    case ESP_RST_SW:       return "Neustart (Software)";
    case ESP_RST_PANIC:    return "Absturz";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "Watchdog";
    case ESP_RST_EXT:      return "Reset-Pin";
    case ESP_RST_USB:      return "USB";
    case ESP_RST_JTAG:     return "JTAG";
    default:               return "unbekannt";
  }
}

// Bildschirmschoner: Rechner 5 min gesperrt -> Schoner, nach Antippen 2 min Ruhe
static bool screenLocked = false;
static uint32_t lockedSince = 0, lastTapMs = 0;
// Ausserhalb der Arbeitszeit (Buchungsfenster bookFrom/bookTo und bookDays): FREI und
// NICHT AM PLATZ werden zu AUSSER HAUS, nach 2 min Ruhe kommt der Bildschirmschoner.
static bool afterHours = false;
static uint32_t afterHoursSince = 0;
static const uint32_t SAVER_AFTER_LOCK_MS = 5UL * 60 * 1000;
static const uint32_t SAVER_AFTER_TAP_MS  = 2UL * 60 * 1000;
static uint32_t shownVersion = 0xFFFFFFFF;
static bool bookingShown() { return demoMode || (bookingEnabled && linked); }
static uint32_t lastMsg = 0;

// ----------------------------------------------------------------- Farben
// Hintergruende kommen aus den Einstellungen (Webinterface). Schrift- und
// Symbolfarbe werden aus der Helligkeit abgeleitet: hell -> dunkle Schrift.
uint16_t c565(uint32_t c) { return lcd.color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }
static int lum(uint32_t c) { return (299 * ((c >> 16) & 0xFF) + 587 * ((c >> 8) & 0xFF) + 114 * (c & 0xFF)) / 1000; }
uint32_t mixRGB(uint32_t a, uint32_t b, int pctB) {        // a -> b um pctB Prozent
  uint32_t o = 0;
  for (int sh = 0; sh <= 16; sh += 8) {
    int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
    o |= (uint32_t)((ca * (100 - pctB) + cb * pctB) / 100) << sh;
  }
  return o;
}
Theme themeFrom(uint32_t bg) {
  uint32_t fg = lum(bg) > 120 ? mixRGB(bg, 0x000000, 85) : 0xFFFFFF;
  return { c565(bg), c565(fg), c565(mixRGB(fg, bg, 30)) };
}
static Theme themeFor(Mode m) {
  switch (m) {
    case M_FREE: return themeFrom(colors.free_);
    case M_CALL: return themeFrom(colors.call);
    case M_BUSY: return themeFrom(colors.busy);
    case M_AWAY:
    case M_OUT:
    case M_CLOSED:  return themeFrom(colors.away);
    default:     return themeFrom(0x505058);
  }
}
uint16_t colLeft() { return c565(colors.left); }
uint32_t inkRGB() {
  return lum(colors.left) > 120 ? mixRGB(colors.left, 0x000000, 75) : mixRGB(colors.left, 0xFFFFFF, 80);
}
uint16_t colInk() { return c565(inkRGB()); }

// ----------------------------------------------------------------- Gestaltung
// Ruhig und sachlich (Oliver, 30.10.2026): hellgraue Kopfzeile
// mit Logo, Datum, Name und Uhrzeit, darunter Weiss. Links Symbol und Zustand, in der Mitte
// eine feine dunkle Linie, rechts daneben ein schmaler Balken in der Statusfarbe, dann die Zeit.
// Graustufen als Anteil Schwarz in Prozent.
static const uint32_t C_BG = 0xFFFFFF;
static const uint32_t C_HDR = 0xCCCCCC;          // Kopfzeile 20 %
static const uint32_t C_TEXT = 0x262626;         // Haupttext 85 %
static const uint32_t C_DIM = 0x737373;          // Beschriftungen 55 %
static const uint32_t C_ICON = 0x404040;         // Symbole 75 %
static const uint32_t C_MID = 0x333333;          // Mittellinie 80 %

// ----------------------------------------------------------------- Symbole
// Klein und grau (Groesse is() = 45 % der frueheren Symbole)
static int is(int v) { return us(v) * 45 / 100; }
static void iconMic(int cx, int cy, uint16_t c) {
  lcd.fillRoundRect(cx - is(32), cy - is(85), is(64), is(110), is(32), c);   // Kapsel
  lcd.fillArc(cx, cy - is(8), is(66), is(55), 0, 180, c);            // Buegel
  lcd.fillRect(cx - is(6), cy + is(56), is(12), is(28), c);              // Stiel
  lcd.fillRoundRect(cx - is(42), cy + is(82), is(84), is(12), is(6), c);     // Fuss
}
static void iconCalendar(int cx, int cy, uint16_t c, uint16_t bg) {
  lcd.fillRoundRect(cx - is(85), cy - is(70), is(170), is(150), is(16), c);
  lcd.fillRoundRect(cx - is(73), cy - is(30), is(146), is(98), is(8), bg);
  lcd.fillRoundRect(cx - is(50), cy - is(92), is(16), is(40), is(8), c);     // Ringe
  lcd.fillRoundRect(cx + is(34), cy - is(92), is(16), is(40), is(8), c);
  for (int r = 0; r < 2; r++)                            // belegte Tage
    for (int k = 0; k < 3; k++)
      lcd.fillRect(cx - is(58) + k * is(44), cy - is(14) + r * is(42), is(28), is(26), c);
}
static void iconCheck(int cx, int cy, uint16_t c) {
  lcd.fillArc(cx, cy, is(88), is(76), 0, 360, c);
  lcd.drawWideLine(cx - is(42), cy + is(2), cx - is(12), cy + is(34), is(7), c);
  lcd.drawWideLine(cx - is(12), cy + is(34), cx + is(46), cy - is(30), is(7), c);
}
static void iconAway(int cx, int cy, uint16_t c, uint16_t bg) {   // Koffer
  lcd.fillRoundRect(cx - is(40), cy - is(78), is(80), is(48), is(14), c);              // Griff
  lcd.fillRoundRect(cx - is(26), cy - is(64), is(52), is(34), is(8), bg);
  lcd.fillRoundRect(cx - is(90), cy - is(40), is(180), is(125), is(16), c);            // Koerper
  lcd.fillRect(cx - is(52), cy - is(40), is(10), is(125), bg);                     // Riemen
  lcd.fillRect(cx + is(42), cy - is(40), is(10), is(125), bg);
}
static void iconKey(int cx, int cy, uint16_t c) {
  lcd.fillArc(cx - is(50), cy, is(48), is(28), 0, 360, c);                     // Ring
  lcd.fillRect(cx - is(4), cy - is(11), is(120), is(22), c);                   // Schaft
  lcd.fillRect(cx + is(70), cy + is(8), is(16), is(30), c);                    // Bart
  lcd.fillRect(cx + is(96), cy + is(8), is(16), is(22), c);
}
// Ampelmaennchen (gehend): NICHT AM PLATZ, also unterwegs. Alpha-Maske aus walker_icon.h
// (tools/make_walker.py, Vorlage Wikimedia Commons, CC BY-SA 2.0 DE, siehe NOTICE.txt),
// gemischt zwischen Hintergrund und Symbolfarbe, um (cx, cy) zentriert.
static void iconWalker(int cx, int cy) {
  const uint8_t* a = WALKER_A;
  const int x0 = cx - WALKER_W / 2, y0 = cy - WALKER_H / 2;
  lcd.startWrite();
  for (int y = 0; y < WALKER_H; y++)
    for (int x = 0; x < WALKER_W; x++, a++)
      if (*a) lcd.drawPixel(x0 + x, y0 + y, c565(mixRGB(C_BG, C_ICON, *a * 100 / 255)));
  lcd.endWrite();
}
static void iconOffline(int cx, int cy, uint16_t c) {
  lcd.fillArc(cx, cy, is(88), is(76), 0, 360, c);
  lcd.drawWideLine(cx - is(56), cy - is(56), cx + is(56), cy + is(56), is(6), c);
}

// ----------------------------------------------------------------- Zeichnen
// Kopfzeile ueber die ganze Breite, darunter links Symbol und Zustand bis zur Mittellinie,
// rechts der Statusbalken und die Zeit. Nach Olivers Skizze: feine dunkle Mittellinie mit
// Abstand oben und unten, rechts daneben mit Abstand ein schmaler, kuerzerer Balken.
static const int BAN_H = uy(64);
static const int SPLIT = SCR_W / 2;              // Mittellinie (2 px)
static const int LINE_Y0 = BAN_H + uy(22), LINE_Y1 = SCR_H - uy(22);
static const int BAR_X = SPLIT + 2 + ux(12), BAR_W = ux(22);
static const int BAR_Y0 = BAN_H + uy(44), BAR_Y1 = SCR_H - uy(44);
static const int PX0 = BAR_X + BAR_W;            // rechte Flaeche
static const int TEXT_CX = PX0 + (SCR_W - PX0) / 2;
static const int LEFT_CX = SPLIT / 2;
// Knopf unten in der linken Flaeche unter dem Zustand (Termin buchen, Wunsch Oliver 30.10.2026)
static const int BK_X = ux(40), BK_W = SPLIT - ux(80), BK_H = uy(68), BK_Y = SCR_H - uy(140);

// Logo links in der Kopfzeile, direkt auf dem Grau wie in der Skizze (3 s druecken = Menue).
// Die Trefferflaeche ist die fruehere Plakette.
static const int BADGE_PX = 8, BADGE_PY = 5;
static const int BADGE_W = LOGO_SMALL_W + 2 * BADGE_PX, BADGE_H = LOGO_SMALL_H + 2 * BADGE_PY;
static const int BADGE_X = ux(20), BADGE_Y = (BAN_H - BADGE_H) / 2;

static void drawTopLogo() {
  const int x0 = BADGE_X + BADGE_PX, y0 = BADGE_Y + BADGE_PY;
  lcd.startWrite();
  const uint8_t* p = LOGO_SMALL_RGBA;
  for (int y = 0; y < LOGO_SMALL_H; y++)
    for (int x = 0; x < LOGO_SMALL_W; x++, p += 4) {
      uint8_t a = pgm_read_byte(p + 3);
      if (!a) continue;
      uint32_t fg = ((uint32_t)pgm_read_byte(p) << 16) | ((uint32_t)pgm_read_byte(p + 1) << 8) | pgm_read_byte(p + 2);
      lcd.drawPixel(x0 + x, y0 + y, c565(mixRGB(C_HDR, fg, a * 100 / 255)));
    }
  lcd.endWrite();
}

// Glatte Schrift (Montserrat-VLW, echte Umlaute). "_" aus dem Protokoll wird
// zum Leerzeichen. Der Hintergrund muss stimmen, damit die Kanten sauber mischen.
static void sTxt(const char* s, int x, int y, int size, uint16_t fg, uint16_t bg,
                 textdatum_t d = top_center) {
  char buf[96];
  strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
  for (char* p = buf; *p; p++) if (*p == '_') *p = ' ';
  lcd.setFont(smooth(size));
  lcd.setTextColor(fg, bg);
  lcd.setTextDatum(d);
  lcd.drawString(buf, x, y);
  lcd.setTextDatum(top_left);
}
// Groesste der Schriften, in der s in w passt
static int fitSize(const char* s, int w, int big, int small) {
  lcd.setFont(smooth(big));
  return lcd.textWidth(s) <= w ? big : small;
}

static uint32_t statusBgRGB(Mode m) {
  switch (m) {
    case M_FREE: return colors.free_;
    case M_CALL: return colors.call;
    case M_BUSY: return colors.busy;
    case M_AWAY:
    case M_OUT:
    case M_CLOSED:  return colors.away;
    default:     return 0x505058;
  }
}

// Verbindung zum Agenten (USB) als Symbol oben rechts in der rechten Flaeche
// (Logos/verbunden.png, Logos/getrennt.png, per tools/make_icons.py als Alpha-Maske):
// geschlossene Linie = verbunden, unterbrochene = getrennt (Form, nicht Farbe).
static void drawConnIcon(bool on, int xRight, int y0) {
  const uint8_t* a = on ? ICON_CONN_ON : ICON_CONN_OFF;
  const int x0 = xRight - ICON_CONN_W;
  lcd.startWrite();
  for (int y = 0; y < ICON_CONN_H; y++)
    for (int x = 0; x < ICON_CONN_W; x++, a++)
      if (*a) lcd.drawPixel(x0 + x, y0 + y, c565(mixRGB(C_BG, C_ICON, *a * 100 / 255)));
  lcd.endWrite();
}

// Datum fuer Bildschirmfotos (USB "DATE 30.02.2026"): ersetzt das echte Datum in der Kopfzeile,
// bis zum Neustart. Ein unmoegliches Datum kennzeichnet Bilder als Demo-Bilder.
static char shotDate[12] = "";
const char* shotDateText() { return shotDate; }

// Kopfzeile: Logo, Datum, Name und Uhrzeit
static void drawHeader() {
  const uint16_t bg = c565(C_HDR), fg = c565(C_TEXT);
  lcd.fillRect(0, 0, SCR_W, BAN_H, bg);
  drawTopLogo();
  if (shotDate[0] || timeValid) {
    struct tm tt; localNow(tt);
    char d[12]; strftime(d, sizeof(d), "%d.%m.%Y", &tt);
    sTxt(shotDate[0] ? shotDate : d, SPLIT - ux(40), BAN_H / 2, 26, fg, bg, middle_right);   // vor der Mitte
  }
  lcd.setFont(smooth(26));
  const int tx = SCR_W - ux(24), tw = lcd.textWidth("00:00");
  sTxt(cur.clock[0] ? cur.clock : "--:--", tx, BAN_H / 2, 26, fg, bg, middle_right);
  const char* name = ownerName.length() ? ownerName.c_str() : account;
  if (*name) {
    const int nx = SPLIT + ux(90), w = tx - tw - ux(36) - nx;
    sTxt(name, nx, BAN_H / 2, fitSize(name, w, 26, 20), fg, bg, middle_left);
  }
}
static void drawClock() { drawHeader(); }

// Rechte Flaeche weiss, Mittellinie, Statusbalken, Verbindung oben rechts
static void drawStatusPane(uint32_t barRGB) {
  lcd.fillRect(0, BAN_H, SCR_W, SCR_H - BAN_H, c565(C_BG));
  lcd.fillRect(SPLIT, LINE_Y0, 2, LINE_Y1 - LINE_Y0, c565(C_MID));
  lcd.fillRect(BAR_X, BAR_Y0, BAR_W, BAR_Y1 - BAR_Y0, c565(barRGB));
  if (demoMode) sTxt("DEMO", SCR_W - ux(24), BAR_Y0, 20, c565(C_DIM), c565(C_BG), top_right);
  else drawConnIcon(linked, SCR_W - ux(24), BAR_Y0);
}

// Knopf unten links: dunkelgrau mit weisser Schrift
static void drawButton(const char* s) {
  lcd.fillRoundRect(BK_X, BK_Y, BK_W, BK_H, us(8), c565(C_ICON));
  sTxt(s, BK_X + BK_W / 2, BK_Y + BK_H / 2, 26, c565(0xFFFFFF), c565(C_ICON), middle_center);
}

enum Icon { IC_MIC, IC_CAL, IC_AWAY, IC_WALK, IC_CHECK, IC_KEY, IC_OFF };

// Links: Symbol, Zustand, Nebenzeilen als Gruppe senkrecht mittig (ueber dem Knopf, wenn er da ist)
static void drawLeft(Icon icon, const char* ttl, const char* sub, const char* sub2) {
  const uint16_t ic = c565(C_ICON), bg = c565(C_BG);
  const int tsz = fitSize(ttl, SPLIT - ux(60), 48, 34);
  lcd.setFont(smooth(tsz));  const int th = lcd.fontHeight();
  lcd.setFont(smooth(26));   const int sh = lcd.fontHeight();
  const int iconH = is(190), gap = uy(28);
  int h = iconH + gap + th + (sub[0] ? uy(8) + sh : 0) + (sub2[0] ? uy(4) + sh : 0);
  const int bottom = bookingShown() && login365.st == L_IDLE ? BK_Y : SCR_H;
  int y = BAN_H + (bottom - BAN_H - h) / 2;
  const int icy = y + is(94);
  switch (icon) {
    case IC_MIC:   iconMic(LEFT_CX, icy, ic); break;
    case IC_CAL:   iconCalendar(LEFT_CX, icy, ic, bg); break;
    case IC_AWAY:  iconAway(LEFT_CX, icy, ic, bg); break;
    case IC_WALK:  iconWalker(LEFT_CX, icy); break;
    case IC_CHECK: iconCheck(LEFT_CX, icy, ic); break;
    case IC_KEY:   iconKey(LEFT_CX - is(30), icy, ic); break;
    default:       iconOffline(LEFT_CX, icy, ic); break;
  }
  y += iconH + gap;
  sTxt(ttl, LEFT_CX, y, tsz, c565(C_TEXT), bg);
  y += th + uy(8);
  if (sub[0]) { sTxt(sub, LEFT_CX, y, 26, c565(C_DIM), bg); y += sh + uy(4); }
  if (sub2[0]) sTxt(sub2, LEFT_CX, y, 26, c565(C_DIM), bg);
}

// Beschriftung, grosse Uhrzeit und Hinweis mittig zwischen top und bottom in der rechten Flaeche.
// Die grosse Schrift hat ueber den Ziffern Leerraum: zuerst die Zeit, dann die Beschriftung
// darueber (VLW fuellt den Zeilenkasten mit dem Hintergrund).
// head (optional): Zustand ueber der Zeit, z. B. ON AIR, pulsiert per onAirLoop().
static int onAirY = -1;                          // Oberkante des ON AIR rechts, -1 = nicht angezeigt
static const uint32_t ONAIR_PERIOD_MS = 2000;    // Takt wie in der Vorversion, nie ganz weg
static const int ONAIR_MIN_PCT = 15;
static void drawOnAirHead(int pct) {
  sTxt("ON AIR", TEXT_CX, onAirY, 48, c565(mixRGB(C_BG, colors.call, pct)), c565(C_BG));
}

static void drawTimeCentered(const char* label, const char* hhmm, const char* note, int top, int bottom,
                             const char* head = "") {
  const uint16_t bg = c565(C_BG), fg = c565(C_TEXT), dim = c565(C_DIM);
  lcd.setFont(smooth(26));  const int sh = lcd.fontHeight();
  lcd.setFont(smooth(48));  const int hh = head[0] ? lcd.fontHeight() + uy(24) : 0;
  lcd.setFont(smooth(34));  const int lh = lcd.fontHeight();
  lcd.setFont(smooth(100)); const int dh = lcd.fontHeight();
  const int asc = dh * 74 / 100;                     // sichtbare Ziffernhoehe im Zeilenkasten
  const int pad = dh - asc - dh * 4 / 100;           // Leerraum ueber den Ziffern
  const int th = hhmm[0] ? lh + uy(16) + asc : 0;
  const int bh = hh + th + (note[0] ? (th ? uy(28) : 0) + sh : 0);
  int y = top + (bottom - top - bh) / 2;
  if (head[0]) { onAirY = y; drawOnAirHead(100); y += hh; }
  if (hhmm[0]) {
    sTxt(hhmm, TEXT_CX, y + lh + uy(16) - pad, 100, fg, bg);
    sTxt(label, TEXT_CX, y, 34, dim, bg);
    y += th + uy(28);
  }
  if (note[0]) sTxt(note, TEXT_CX, y, 26, dim, bg);
}

// Nur auf der Statusanzeige im Zustand ON AIR, 20 Stufen je Halbtakt
static void onAirLoop() {
  static int lastStep = -1;
  if (onAirY < 0 || cur.mode != M_CALL || shown.mode != M_CALL || bookingActive() || saverActive() ||
      menuActive() || login365.st != L_IDLE) { lastStep = -1; return; }
  uint32_t ph = millis() % ONAIR_PERIOD_MS, half = ONAIR_PERIOD_MS / 2;
  int step = (ph < half ? half - ph : ph - half) * 20 / half;      // 20 = kraeftig ... 0 = schwach
  if (step == lastStep) return;
  lastStep = step;
  drawOnAirHead(ONAIR_MIN_PCT + step * (100 - ONAIR_MIN_PCT) / 20);
}

// Office-365-Anmeldung: links Schluessel, rechts Adresse und Code
static void drawLogin() {
  const uint16_t bg = c565(C_BG), fg = c565(C_TEXT), dim = c565(C_DIM);
  drawHeader();
  onAirY = -1;
  drawStatusPane(0x505058);
  drawLeft(IC_KEY, "OFFICE 365", "", "");
  const int cy = BAN_H + (SCR_H - BAN_H) / 2;
  switch (login365.st) {
    case L_REQUESTED:
      sTxt("Warte auf den Agenten ...", TEXT_CX, cy, 26, dim, bg, middle_center);
      break;
    case L_CODE: {
      const char* u = login365.url;
      if (!strncmp(u, "https://", 8)) u += 8;
      sTxt("Am PC oder Handy \xC3\xB6" "ffnen:", TEXT_CX, cy - uy(120), 26, dim, bg);
      sTxt(u, TEXT_CX, cy - uy(80), 26, fg, bg);
      sTxt("und diesen Code eingeben:", TEXT_CX, cy - uy(10), 26, dim, bg);
      sTxt(login365.code, TEXT_CX, cy + uy(30), 48, fg, bg);
      break;
    }
    case L_OK:
      sTxt("Angemeldet als", TEXT_CX, cy - uy(30), 26, dim, bg);
      sTxt(login365.msg, TEXT_CX, cy + uy(10), 26, fg, bg);
      break;
    default:
      sTxt("FEHLER", TEXT_CX, cy - uy(40), 48, fg, bg);
      sTxt(login365.msg, TEXT_CX, cy + uy(30), 20, fg, bg);
      break;
  }
}

// Statusanzeige: links Symbol und Zustand, rechts Beschriftung und Zeit, beides
// senkrecht mittig in der eigenen Flaeche
static void drawFull() {
  const uint16_t bg = c565(C_BG), fg = c565(C_TEXT), dim = c565(C_DIM);
  drawHeader();
  onAirY = -1;
  drawStatusPane(statusBgRGB(cur.mode));

  Icon icon = IC_OFF;
  const char *ttl = "KEIN SIGNAL", *sub = cur.src, *sub2 = "", *label = "", *hhmm = "", *note = "";
  char lbuf[32] = "", nbuf[40] = "", cbuf[48] = "";
  // Gespraech mit bekanntem Zustand darunter: diesen Zustand mit seinen Zeiten zeigen, ON AIR
  // pulsiert rechts darueber. So sieht ein Besucher, ob ein Termin laeuft und wann wieder frei.
  const bool air = cur.mode == M_CALL;
  Mode sm = cur.mode;
  if (air && (cur.base == M_FREE || cur.base == M_BUSY || cur.base == M_AWAY)) sm = cur.base;
  switch (sm) {
    case M_CALL:
      icon = IC_MIC; ttl = "IM GESPR\xC3\x84" "CH";     // ON AIR pulsiert rechts ueber der Zeit
      if (cur.freeAt[0]) { label = "Termin bis"; hhmm = cur.freeAt; }
      break;
    case M_BUSY:
      icon = IC_CAL; ttl = "BESETZT";
      if (cur.freeAt[0]) { label = cur.offline ? "zur\xC3\xBC" "ck ca." : "frei ab"; hhmm = cur.freeAt; }
      break;
    case M_AWAY:
      icon = IC_AWAY; ttl = "ABWESEND";
      if (cur.freeAt[0]) { label = "zur\xC3\xBC" "ck ab"; hhmm = cur.freeAt; }
      break;
    case M_OUT:
      icon = IC_WALK; ttl = "NICHT AM PLATZ";
      if (cur.nextAt[0]) { label = "n\xC3\xA4" "chster Termin"; hhmm = cur.nextAt; }
      else note = "heute keine Termine mehr";
      break;
    case M_CLOSED:
      icon = IC_AWAY; ttl = "AUSSER HAUS";
      if (cur.freeAt[0]) {
        if (!strcmp(cur.nextAt, "heute")) strcpy(lbuf, "zur\xC3\xBC" "ck ab");
        else snprintf(lbuf, sizeof(lbuf), "zur\xC3\xBC" "ck %s ab", cur.nextAt);
        label = lbuf; hhmm = cur.freeAt;
      }
      break;
    case M_FREE:
      icon = IC_CHECK; ttl = "FREI";
      if (cur.nextAt[0]) {
        label = "frei bis"; hhmm = cur.nextAt;
        if (cur.freeAt[0]) { snprintf(nbuf, sizeof(nbuf), "danach wieder frei ab %s", cur.freeAt); note = nbuf; }
      } else note = "heute keine Termine mehr";
      break;
    default:
      sub = "Agent auf dem"; sub2 = "Computer starten";
      break;
  }
  if (air && sm != M_CALL) {                     // links unter dem Zustand: im Gespraech, Quelle
    snprintf(cbuf, sizeof(cbuf), "im Gespr\xC3\xA4" "ch%s%s", cur.src[0] ? ", " : "", cur.src);
    sub = cbuf; sub2 = "";
  }
  drawLeft(icon, ttl, sub, sub2);

  // Rechts: bei ON AIR pulsierend der Zustand, darunter Beschriftung und Uhrzeit, mittig
  drawTimeCentered(label, hhmm, note, BAN_H, SCR_H, cur.mode == M_CALL ? "ON AIR" : "");
  if (bookingShown()) drawButton("Termin buchen");   // ohne Notebook keine Buchung moeglich
  shown = cur;
}

static void render() {
  if (bookingActive() || saverActive() || menuActive()) return;  // zeichnen selbst
  bool full = cur.mode != shown.mode || strcmp(cur.freeAt, shown.freeAt) || strcmp(cur.nextAt, shown.nextAt) ||
              strcmp(cur.src, shown.src) || cur.base != shown.base || uiVersion != shownVersion || linked != shownLinked;
  if (full) {
    shownVersion = uiVersion;
    shownLinked = linked;
    if (login365.st != L_IDLE) { drawLogin(); shown = cur; }
    else drawFull();
    return;
  }
  if (strcmp(cur.clock, shown.clock)) {
    drawClock();
    strcpy(shown.clock, cur.clock);
  }
}

// ----------------------------------------------------------------- Seriell
static void field(char* dst, size_t n, const char* tok) {
  if (!tok || strcmp(tok, "-") == 0) { dst[0] = '\0'; return; }
  strncpy(dst, tok, n - 1); dst[n - 1] = '\0';
}

static void handleLine(char* line) {
  if (bookingSerial(line)) return;
  if (strncmp(line, "HELLO", 5) == 0) { Serial.println("TALLY32 1"); return; }
  if (strncmp(line, "TAP ", 4) == 0) {           // Diagnose: Antippen nachstellen (Bildschirmfotos)
    int x = 0, y = 0;                            // TAP menu = Menue oeffnen (wie 3 s auf das Logo)
    if (!strcmp(line + 4, "menu")) menuOpen();
    else if (sscanf(line + 4, "%d %d", &x, &y) == 2) {
      if (menuActive()) menuTap(x, y);
      else if (bookingActive()) bookingTap(x, y);
      else if (bookingShown() && x >= BK_X && x < BK_X + BK_W && y >= BK_Y && y < BK_Y + BK_H) bookingOpen();
    }
    Serial.println("OK");
    return;
  }
  if (strcmp(line, "STATUS") == 0) {             // Diagnose: angezeigter Zustand
    Serial.printf("STATUS mode=%d offline=%d clock=%s frei_ab=%s frei_bis=%s quelle=%s linked=%d booking_btn=%d booking=%d demo=%d locked=%d saver=%d afterhours=%d menu=%d tapage=%lu hoursage=%lu bookfrom=%u ap=%d rtc=%d reset=%s\n",
                  (int)cur.mode, (int)cur.offline, cur.clock, cur.freeAt, cur.nextAt, cur.src,
                  (int)linked, (int)bookingShown(), (int)bookingActive(), (int)demoMode,
                  (int)screenLocked, (int)saverActive(), (int)afterHours, (int)menuActive(),
                  (unsigned long)((millis() - lastTapMs) / 1000), (unsigned long)((millis() - afterHoursSince) / 1000), (unsigned)bookFrom, webApState(), rtcState, resetReasonText());
    return;
  }
  if (strcmp(line, "SHOT") == 0) {               // Diagnose: Bildschirm als RGB565 (zeilenweise) ueber USB
    static uint16_t row[SCR_W];
    Serial.setTxTimeoutMs(3000);
    Serial.printf("SHOT %d %d\n", SCR_W, SCR_H);
    for (int y = 0; y < SCR_H; y++) {
      lcd.readRect(0, y, SCR_W, 1, row);
      Serial.write((const uint8_t*)row, sizeof(row));
    }
    Serial.flush();
    Serial.println("SHOT END");
    return;
  }
  if (strncmp(line, "SET ", 4) == 0) {
    char key[16]; int v;
    if (sscanf(line + 4, "%15s %d", key, &v) == 2) {
      if (!strcmp(key, "demo")) { demoMode = v != 0; demoSave(); Serial.println("OK"); return; }
      if (!strcmp(key, "saver")) { if (v) saverStart(); else saverStop(); Serial.println("OK"); return; }   // Test
#if defined(BOARD_JC8012)
      if (!strcmp(key, "touchlog")) { gsl_log = v != 0; Serial.println("OK"); return; }   // Rohbytes des Touch
      if (!strcmp(key, "touchinit")) { gsl_ok = false; gsl_begin(TOUCH_RST, TOUCH_INT); Serial.println("OK"); return; }
#endif
      if (!strcmp(key, "flip")) flip = v != 0;
      else if (!strcmp(key, "bright")) brightness = v < 12 ? 12 : (v > 255 ? 255 : v);
      displayApply(true);
      Serial.println("OK");
    }
    return;
  }
  if (strncmp(line, "DATE ", 5) == 0) {          // Diagnose: Datum fuer Bildschirmfotos ("DATE -" = echtes)
    field(shotDate, sizeof(shotDate), line + 5);
    uiVersion++;
    Serial.println("OK");
    return;
  }
  if (strncmp(line, "AC ", 3) == 0) {            // angemeldetes Konto (Banner, wenn kein Name gesetzt)
    char acc[64]; field(acc, sizeof(acc), line + 3);
    if (strcmp(acc, account)) { strcpy(account, acc); if (!ownerName.length()) uiVersion++; }
    return;
  }
  if (strncmp(line, "CAP write ", 10) == 0) { canWrite = line[10] == '1'; return; }
  if (strncmp(line, "LK ", 3) == 0) {            // Bildschirmsperre des Rechners
    bool lk = line[3] == '1';
    if (lk && !screenLocked) lockedSince = millis();
    screenLocked = lk;
    return;
  }
  if (strncmp(line, "TM ", 3) == 0) {            // Uhrzeit fuer den Offline-Betrieb
    long long utc = 0; long off = 0;
    if (sscanf(line + 3, "%lld %ld", &utc, &off) == 2 && utc > 1700000000LL) {
      struct timeval tv = { (time_t)utc, 0 };
      settimeofday(&tv, nullptr);
      tzOffset = off;
      timeValid = true;
      // Echtzeituhr nachfuehren: beim ersten Mal sofort, danach alle 10 min
      if (rtc_present && (rtcState != RTC_OK || millis() - rtcWrittenMs > 10UL * 60 * 1000)) {
        if (rtc_write((time_t)utc)) { rtcState = RTC_OK; rtcWrittenMs = millis(); }
      }
    }
    return;
  }
  if (strncmp(line, "CB ", 3) == 0) {            // Terminbloecke eines Tages
    char date[11] = "", list[256] = "";
    if (sscanf(line + 3, "%10s %255s", date, list) < 1) return;
    int slot = !strcmp(calDays[0].date, date) ? 0 : !strcmp(calDays[1].date, date) ? 1
             : (strcmp(calDays[0].date, calDays[1].date) < 0 ? 0 : 1);   // aelteren Tag ersetzen
    CalDay& d = calDays[slot];
    strcpy(d.date, date);
    d.n = 0;
    if (strcmp(list, "-"))
      for (char* p = strtok(list, ","); p && d.n < 16; p = strtok(nullptr, ",")) {
        int a = 0, b = 0; char k = 'b';
        if (sscanf(p, "%d-%d%c", &a, &b, &k) >= 2)
          d.b[d.n++] = { (uint16_t)(a / 100 * 60 + a % 100), (uint16_t)(b / 100 * 60 + b % 100), k == 'a' };
      }
    if (timeValid) calSyncUtc = time(nullptr);
    cacheSave();
    return;
  }
  if (strncmp(line, "LC ", 3) == 0) {            // Device Code fuer die Anmeldung
    char code[24] = "", url[80] = "";
    sscanf(line + 3, "%23s %79s", code, url);
    strcpy(login365.code, code);
    strcpy(login365.url, url);
    login365.st = L_CODE;
    login365.since = millis();
    uiVersion++;
    return;
  }
  if (strncmp(line, "LR ", 3) == 0) {            // Ergebnis der Anmeldung
    bool ok = !strncmp(line + 3, "OK", 2);
    const char* rest = line + 5; if (*rest == ' ') rest++;
    field(login365.msg, sizeof(login365.msg), rest);
    if (ok) field(account, sizeof(account), rest);
    login365.st = ok ? L_OK : L_ERR;
    login365.since = millis();
    uiVersion++;
    return;
  }
  if (strncmp(line, "ST ", 3) != 0) return;
  char* save = nullptr;
  strtok_r(line, " ", &save);
  const char* mode = strtok_r(nullptr, " ", &save);
  const char* clk  = strtok_r(nullptr, " ", &save);
  const char* fa   = strtok_r(nullptr, " ", &save);
  const char* na   = strtok_r(nullptr, " ", &save);
  const char* src  = strtok_r(nullptr, " ", &save);
  const char* base = strtok_r(nullptr, " ", &save);   // nur beim Gespraech: Zustand darunter
  if (!mode) return;
  if (demoMode) { lastMsg = millis(); linked = true; Serial.println("OK"); return; }   // Anzeige gehoert der Demo
  if      (!strcmp(mode, "call")) cur.mode = M_CALL;
  else if (!strcmp(mode, "busy")) cur.mode = M_BUSY;
  else if (!strcmp(mode, "away")) cur.mode = M_AWAY;
  else                            cur.mode = M_FREE;
  field(cur.clock, sizeof(cur.clock), clk);
  field(cur.freeAt, sizeof(cur.freeAt), fa);
  field(cur.nextAt, sizeof(cur.nextAt), na);
  field(cur.src, sizeof(cur.src), src);
  cur.base = !base ? M_OFFLINE : !strcmp(base, "busy") ? M_BUSY : !strcmp(base, "away") ? M_AWAY
           : !strcmp(base, "free") ? M_FREE : M_OFFLINE;
  cur.offline = false;
  cur.stand[0] = '\0';
  strcpy(lastStand, cur.clock);
  lastMsg = millis();
  linked = true;
  Serial.println("OK");
}

static void pollSerial() {
  static char buf[512];
  static int len = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[len] = '\0'; if (len) handleLine(buf); len = 0; continue; }
    if (len < (int)sizeof(buf) - 1) buf[len++] = c;
  }
}

// ----------------------------------------------------------------- Touch
// Antippen: Buchungsknopf bzw. Buchungsdialog. 5 s gedrueckt halten bei
// abgeschaltetem Einrichtungs-WLAN -> Access Point wieder ein. 10 s gedrueckt
// halten -> Web- und WLAN-Passwort zuruecksetzen (wer am Geraet steht, darf das).
static const uint32_t AP_ON_HOLD_MS = 5000;
static void mapTouch(int rx, int ry, int& x, int& y) {
  if (flip) { x = (SCR_W - 1) - rx; y = (SCR_H - 1) - ry; }
  else      { x = rx;               y = ry; }
}

static const uint32_t MENU_HOLD_MS = 3000;       // so lange auf die Plakette -> Menue
static const int BADGE_HIT = us(16);                 // Trefferflaeche um die Plakette

static void touchLoop() {
  static bool held = false, prev = false, waitRelease = false, onBadge = false;
  static int rx = 0, ry = 0, count = 0;
  static uint32_t since = 0;
  GtPoint pts[5];
  int nt = board_touch_read(pts, 5);
  bool extra = false;                            // weiterer Finger, waehrend der vorige noch aufliegt
  if (nt >= 0) {                                 // kein frisches Sample -> Zustand halten
    held = nt > 0;
    extra = held && prev && nt > count && bookingActive();   // zuegiges Tippen auf der Tastatur
    if (held) { rx = pts[nt - 1].x; ry = pts[nt - 1].y; }    // neuester Finger zuletzt
    count = nt;
  }
  if ((held && !prev) || extra) {                // Antippen (Druckbeginn)
    lastTapMs = millis();
    if (saverActive()) { saverStop(); prev = held; return; }   // nur aufwecken
    int x, y;
    mapTouch(rx, ry, x, y);
    static int logged = 0;
    if (logged < 20) { logged++; Serial.printf("LOG tap raw %d %d -> %d %d\n", rx, ry, x, y); }
    onBadge = !bookingActive() && !menuActive() &&
              x >= BADGE_X - BADGE_HIT && x < BADGE_X + BADGE_W + BADGE_HIT &&
              y >= BADGE_Y - BADGE_HIT && y < BADGE_Y + BADGE_H + BADGE_HIT;
    if (menuActive()) { menuTap(x, y); waitRelease = true; }   // im Menue keine Haltegesten
    else if (bookingActive()) bookingTap(x, y);
    else if (bookingShown() && x >= BK_X && x < BK_X + BK_W && y >= BK_Y && y < BK_Y + BK_H) bookingOpen();
  }
  prev = held;
  if (!held) { since = 0; waitRelease = false; return; }
  if (waitRelease) return;
  if (!since) { since = millis(); return; }
  if (onBadge && millis() - since >= MENU_HOLD_MS) {
    menuOpen();
    waitRelease = true;                          // keine weiteren Haltegesten im selben Druck
    return;
  }
  if (apOff && millis() - since >= AP_ON_HOLD_MS) {
    apOff = false;
    apOffSave();
    apApply();
    Theme t = themeFrom(0x2A3240);
    lcd.fillScreen(t.bg);
    sTxt("Einrichtungs-WLAN eingeschaltet", SCR_W / 2, ly(180), 34, t.fg, t.bg);
    char b[48];
    snprintf(b, sizeof(b), "WLAN %s, http://192.168.4.1", webSsid());
    sTxt(b, SCR_W / 2, ly(250), 26, t.dim, t.bg);
    delay(3000);
    uiVersion++;                                 // Anzeige neu zeichnen
    waitRelease = true;                          // kein Zugangs-Reset im selben Druck
    return;
  }
  if (millis() - since < 10000) return;
  accessReset();
  Theme t = themeFrom(0x2A3240);
  lcd.fillScreen(t.bg);
  sTxt("Zugang zur\xC3\xBC" "ckgesetzt", SCR_W / 2, ly(180), 34, t.fg, t.bg);
  sTxt("Web: admin / tally32, WLAN-Passwort Standard, PIN 0000", SCR_W / 2, ly(250), 26, t.dim, t.bg);
  delay(4000);
  ESP.restart();
}

// ----------------------------------------------------------------- Offline-Betrieb
static const CalDay* calFor(const char* date) {
  for (auto& d : calDays) if (!strcmp(d.date, date)) return &d;
  return nullptr;
}

// Status aus dem Offline-Puffer: laufender Block -> besetzt/abwesend mit
// Ende, sonst "nicht am Platz" mit dem naechsten Termin. Ohne gueltige Daten
// (keine Uhrzeit, zu alt, Tag fehlt) -> KEIN SIGNAL.
static void offlineStatus() {
  static uint32_t lastRun = 0;
  if (millis() - lastRun < 1000) return;
  lastRun = millis();
  Status s = { M_OFFLINE, "", "", "", "", false, "" };
  time_t age = time(nullptr) - calSyncUtc;
  if (timeValid && calSyncUtc && age >= -60 && age < CACHE_MAX_S) {
    time_t now = time(nullptr) + tzOffset;
    struct tm t; gmtime_r(&now, &t);
    char date[11]; strftime(date, sizeof(date), "%Y-%m-%d", &t);
    int m = t.tm_hour * 60 + t.tm_min;
    const CalDay* d = calFor(date);
    if (d) {
      snprintf(s.clock, sizeof(s.clock), "%02d:%02d", t.tm_hour, t.tm_min);
      s.offline = true;
      strcpy(s.stand, lastStand);
      snprintf(s.src, sizeof(s.src), "Stand %s", lastStand);
      const CalBlock* curB = nullptr;
      for (int i = 0; i < d->n; i++) if (d->b[i].from <= m && m < d->b[i].to) curB = &d->b[i];
      if (curB) {
        s.mode = curB->away ? M_AWAY : M_BUSY;
        if (curB->to >= 1440) {                  // geht ueber Mitternacht: Datum zeigen
          time_t tm2 = now + 86400; struct tm t2; gmtime_r(&tm2, &t2);
          strftime(s.freeAt, sizeof(s.freeAt), "%d.%m.", &t2);
        } else snprintf(s.freeAt, sizeof(s.freeAt), "%02d:%02d", curB->to / 60, curB->to % 60);
      } else {
        s.mode = M_OUT;
        int next = -1;
        for (int i = 0; i < d->n; i++) if (d->b[i].from > m && (next < 0 || d->b[i].from < next)) next = d->b[i].from;
        if (next >= 0) snprintf(s.nextAt, sizeof(s.nextAt), "%02d:%02d", next / 60, next % 60);
      }
    }
  }
  cur = s;
}

// ----------------------------------------------------------------- Demomodus
time_t localEpoch() {
  if (timeValid) return time(nullptr) + tzOffset;
  return 1793008800 + millis() / 1000;           // Ersatz: Montag 26.10.2026 10:00
}

void localNow(struct tm& t) {
  time_t now = localEpoch();
  gmtime_r(&now, &t);
}

// Zeit in min Minuten ab jetzt, auf die Viertelstunde gerundet
static void demoTime(int min, char* out) {
  struct tm t; localNow(t);
  int m = (t.tm_hour * 60 + t.tm_min + min + 7) / 15 * 15;
  if (m > 23 * 60 + 45) m = 23 * 60 + 45;
  snprintf(out, 8, "%02d:%02d", m / 60, m % 60);
}

// Reihum alle Zustaende, einmal pro Runde eine vorgefuehrte Buchung.
// Waehrend ein Besucher selbst bucht, pausiert die Runde.
static void demoLoop() {
  static int step = -1;
  static uint32_t next = 0;
  static const uint32_t SHOW_MS = 6000;
  if (bookingActive()) { next = millis() + 2000; return; }
  struct tm t; localNow(t);
  snprintf(cur.clock, sizeof(cur.clock), "%02d:%02d", t.tm_hour, t.tm_min);
  if (millis() < next) return;
  step = (step + 1) % 9;
  next = millis() + SHOW_MS;
  Status s = { M_FREE, "", "", "", "", false, "" };
  strcpy(s.clock, cur.clock);
  switch (step) {
    case 0: demoTime(45, s.nextAt); demoTime(75, s.freeAt); break;              // frei bis, danach frei ab
    case 1: s.mode = M_BUSY; demoTime(30, s.freeAt); strcpy(s.src, "Termin im Kalender"); break;
    case 2: s.mode = M_CALL; s.base = M_BUSY; demoTime(30, s.freeAt); strcpy(s.src, "Teams"); break;   // im Termin
    case 3: s.mode = M_CALL; s.base = M_FREE; demoTime(45, s.nextAt); demoTime(75, s.freeAt);       // spontan
            strcpy(s.src, "3CX"); break;
    case 4: s.mode = M_AWAY; strcpy(s.freeAt, "17:00"); strcpy(s.src, "laut Kalender"); break;
    case 5: s.mode = M_OUT; s.offline = true; demoTime(60, s.nextAt);
            demoTime(-20, s.stand); snprintf(s.src, sizeof(s.src), "Stand %s", s.stand); break;
    case 6: cur = s; bookingDemoStart(); return;                                // Buchung vorfuehren
    case 8: s.mode = M_CLOSED; strcpy(s.src, "Feierabend"); strcpy(s.freeAt, "07:00");
            strcpy(s.nextAt, "morgen"); break;                                  // ausserhalb der Arbeitszeit
    default: break;                                                             // frei, keine Termine
  }
  cur = s;
}

// ----------------------------------------------------------------- Feierabend
static const char* WD_SHORT[7] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };

static void applyAfterHours() {
  bool now_after = false;
  struct tm t;
  if (timeValid && !demoMode && login365.st == L_IDLE) {
    localNow(t);
    int wd = (t.tm_wday + 6) % 7, m = t.tm_hour * 60 + t.tm_min;
    bool workday = bookDays & (1 << wd);
    now_after = !(workday && m >= bookFrom && m < bookTo);
  }
  if (now_after && !afterHours) afterHoursSince = millis();
  afterHours = now_after;
  if (!afterHours || !(cur.mode == M_FREE || cur.mode == M_OUT)) return;

  // Anrufe und Termine bleiben sichtbar, nur "frei" wird zu "ausser Haus"
  cur.mode = M_CLOSED;
  int wd = (t.tm_wday + 6) % 7, m = t.tm_hour * 60 + t.tm_min;
  strcpy(cur.src, (bookDays & (1 << wd)) ? "Feierabend" : "Arbeitsfreier Tag");
  cur.freeAt[0] = cur.nextAt[0] = '\0';
  for (int d = 0; d < 8; d++) {                  // naechster Arbeitsbeginn
    int day = (wd + d) % 7;
    if (!(bookDays & (1 << day))) continue;
    if (d == 0 && m >= bookFrom) continue;       // heute schon vorbei
    snprintf(cur.freeAt, sizeof(cur.freeAt), "%02u:%02u", bookFrom / 60, bookFrom % 60);
    strcpy(cur.nextAt, d == 0 ? "heute" : d == 1 ? "morgen" : WD_SHORT[day]);
    break;
  }
}

// ----------------------------------------------------------------- Setup / Loop
void setup() {
  // USB-Serial/JTAG (HWCDC) puffert standardmaessig nur 256 Bytes und verwirft den
  // Rest. Die freien Zeiten der Buchung (SL, rund 1 KB) und der Offline-Puffer (CB)
  // kommen am Stueck, darum ein grosser Empfangspuffer. Muss vor begin() stehen.
  Serial.setRxBufferSize(16384);
  Serial.begin(115200);
  delay(200);
  board_init();
  // Uhrzeit und Offline-Puffer vom letzten Lauf: nach einem kurzen Stromunterbruch
  // rechnet das Display ohne Agent weiter (Zeit nur mit Knopfzelle in der Uhr)
  cacheLoad();
  time_t rtcUtc = 0;
  rtcState = rtc_begin() ? rtc_read(rtcUtc) : RTC_NONE;
  if (rtcState == RTC_OK && calSyncUtc) {         // Zeitzone kommt aus dem Puffer
    struct timeval tv = { rtcUtc, 0 };
    settimeofday(&tv, nullptr);
    timeValid = true;
  }
  ledcWrite(TFT_BL, 0);                          // erst einschalten, wenn das Logo steht
  colorsLoad();
  displayLoad();
  bookingLoad();
  board_set_rotation(flip);
  bootScreen();
  displayApply(false);
  uint32_t t0 = millis();
  bootProgress(20);
  webBegin();
  bootProgress(60);
  while (millis() - t0 < 2500) {                 // Logo mindestens 2,5 s zeigen
    bootProgress(60 + (millis() - t0) * 40 / 2500);
    pollSerial();
    delay(30);
  }
  bootProgress(100);
  delay(150);
  drawFull();
  Serial.println("TALLY32 1");
}

void loop() {
  pollSerial();
  webLoop();
  login365Loop();

  if (linked && millis() - lastMsg > LINK_TIMEOUT_MS) {
    linked = false;
    if (!demoMode) bookingCancel();              // Buchung geht nur mit Notebook
  }
  if (demoMode && !menuActive()) demoLoop();
  else if (!linked) offlineStatus();
  applyAfterHours();
  bookingLoop();
  // Bildschirmschoner: bei gesperrtem Rechner (5 min) oder ausserhalb der Arbeitszeit
  // (2 min), nicht in Demo/Buchung/Menue/Anmeldung
  bool quiet = !demoMode && !bookingActive() && !menuActive() && login365.st == L_IDLE;
  bool byLock = quiet && screenLocked && linked;
  bool byHours = quiet && afterHours && cur.mode == M_CLOSED;
  if (saverActive()) { if (!byLock && !byHours) saverStop(); else saverLoop(); }
  else if (millis() - lastTapMs >= SAVER_AFTER_TAP_MS &&
           ((byLock && millis() - lockedSince >= SAVER_AFTER_LOCK_MS) ||
            (byHours && millis() - afterHoursSince >= SAVER_AFTER_TAP_MS))) saverStart();
  menuLoop();
  render();
  onAirLoop();
  touchLoop();

  delay(10);
}
