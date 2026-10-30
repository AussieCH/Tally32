/******************************************************************************
 * settings.cpp  -  Einstellungen im NVS-Namespace "tally"
 *   Farben, Helligkeit/Drehung, Web-Passwort (SHA-256 mit Salz), WLAN-Passwort,
 *   Einrichtungs-WLAN ein/aus, PIN des Einstellungsmenues am Geraet
 *   sowie der Ablauf der Office-365-Anmeldung ueber den Agent.
 ******************************************************************************/
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include "board.h"
#include "state.h"

// Standard: Statusfarben unterscheiden sich klar in der Helligkeit
// (frei hell, besetzt mittel, Gespraech/abwesend dunkel, rot vs. blau).
const Colors DEFAULT_COLORS = { 0xD2BA62, 0xA8E6A0, 0xE06A00, 0x1A2A6A, 0x8C0810 };
Colors colors = DEFAULT_COLORS;
uint8_t brightness = 200;
bool flip = false;
uint8_t uiLang = 0;
String ownerName;
Login365 login365 = { L_IDLE, "", "", "", 0 };
bool bookingEnabled = false;
uint16_t bookFrom = 8 * 60, bookTo = 17 * 60;
uint8_t bookDays = 0x1F;                         // Montag bis Freitag
bool lunchOn = false;
bool demoMode = false;
uint16_t lunchFrom = 12 * 60, lunchTo = 13 * 60;
bool apOff = false;

static const char* NS = "tally";

// ------------------------------------------------------------ Farben
void colorsLoad() {
  Preferences p; p.begin(NS, true);
  colors.left  = p.getUInt("c_left", DEFAULT_COLORS.left);
  colors.free_ = p.getUInt("c_free", DEFAULT_COLORS.free_);
  colors.busy  = p.getUInt("c_busy", DEFAULT_COLORS.busy);
  colors.away  = p.getUInt("c_away", DEFAULT_COLORS.away);
  colors.call  = p.getUInt("c_call", DEFAULT_COLORS.call);
  p.end();
}

void colorsSave() {
  Preferences p; p.begin(NS, false);
  p.putUInt("c_left", colors.left);
  p.putUInt("c_free", colors.free_);
  p.putUInt("c_busy", colors.busy);
  p.putUInt("c_away", colors.away);
  p.putUInt("c_call", colors.call);
  p.end();
  uiVersion++;
}

// ------------------------------------------------------------ Anzeige
void displayLoad() {
  Preferences p; p.begin(NS, true);
  flip = p.getBool("flip", false);
  brightness = p.getUChar("bright", 200);
  uiLang = p.getUChar("lang", 0) ? 1 : 0;
  ownerName = p.getString("owner", "");
  p.end();
}

void displayApply(bool save) {
  if (brightness < 12) brightness = 12;
  board_set_rotation(flip);
  ledcWrite(TFT_BL, brightness);
  if (save) {
    Preferences p; p.begin(NS, false);
    p.putBool("flip", flip);
    p.putUChar("bright", brightness);
    p.putUChar("lang", uiLang);
    p.putString("owner", ownerName);
    p.end();
  }
  uiVersion++;
}

// ------------------------------------------------------------ Terminbuchung
void bookingLoad() {
  Preferences p; p.begin(NS, true);
  bookingEnabled = p.getBool("bk_on", false);
  bookFrom = p.getUShort("bk_from", 8 * 60);
  bookTo   = p.getUShort("bk_to", 17 * 60);
  bookDays = p.getUChar("bk_days", 0x1F);
  lunchOn   = p.getBool("bk_lon", false);
  lunchFrom = p.getUShort("bk_lfrom", 12 * 60);
  lunchTo   = p.getUShort("bk_lto", 13 * 60);
  demoMode  = p.getBool("demo", false);
  p.end();
}

void bookingSave() {
  Preferences p; p.begin(NS, false);
  p.putBool("bk_on", bookingEnabled);
  p.putUShort("bk_from", bookFrom);
  p.putUShort("bk_to", bookTo);
  p.putUChar("bk_days", bookDays);
  p.putBool("bk_lon", lunchOn);
  p.putUShort("bk_lfrom", lunchFrom);
  p.putUShort("bk_lto", lunchTo);
  p.end();
  uiVersion++;
}

// Befehl an den Agenten auf dem Computer (USB)
void sendCmd(const char* line) { Serial.println(line); }

// Sprache setzen (0 Deutsch, 1 English): speichern, neu zeichnen und dem Agenten melden,
// damit er seine Texte fuers Display (Quelle, Fehlermeldungen) in derselben Sprache schickt.
void langSet(uint8_t l) {
  uiLang = l ? 1 : 0;
  displayApply(true);
  sendCmd(uiLang ? "LANG en" : "LANG de");
}

// ------------------------------------------------------------ Demomodus
void demoSave() {
  Preferences p; p.begin(NS, false);
  p.putBool("demo", demoMode);
  p.end();
  uiVersion++;
}

// ------------------------------------------------------------ Zugang
static String sha256Hex(const String& s) {
  uint8_t out[32];
  mbedtls_sha256((const unsigned char*)s.c_str(), s.length(), out, 0);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", out[i]);
  return String(hex);
}

bool authMustChange() {
  Preferences p; p.begin(NS, true);
  bool none = p.getString("pw_hash", "").isEmpty();
  p.end();
  return none;
}

bool authCheck(const String& pw) {
  Preferences p; p.begin(NS, true);
  String salt = p.getString("pw_salt", "");
  String hash = p.getString("pw_hash", "");
  p.end();
  if (hash.isEmpty()) return pw == WEB_DEFAULT_PW;
  return sha256Hex(salt + pw) == hash;
}

void authSet(const String& pw) {
  char salt[33];
  for (int i = 0; i < 16; i++) sprintf(salt + 2 * i, "%02x", (unsigned)(esp_random() & 0xFF));
  Preferences p; p.begin(NS, false);
  p.putString("pw_salt", salt);
  p.putString("pw_hash", sha256Hex(String(salt) + pw));
  p.end();
}

String apPassLoad() {
  Preferences p; p.begin(NS, true);
  String s = p.getString("ap_pass", AP_DEFAULT_PASS);
  p.end();
  return s;
}

void apPassSave(const String& pass) {
  Preferences p; p.begin(NS, false);
  p.putString("ap_pass", pass);
  p.end();
}

String pinLoad() {
  Preferences p; p.begin(NS, true);
  String s = p.getString("pin", MENU_DEFAULT_PIN);
  p.end();
  return pinValid(s) ? s : String(MENU_DEFAULT_PIN);
}

void pinSave(const String& pin) {
  if (!pinValid(pin)) return;
  Preferences p; p.begin(NS, false);
  p.putString("pin", pin);
  p.end();
}

bool pinValid(const String& pin) {
  if (pin.length() != 4) return false;
  for (char c : pin) if (c < '0' || c > '9') return false;
  return true;
}

void accessReset() {
  Preferences p; p.begin(NS, false);
  p.remove("pin");
  p.remove("pw_salt");
  p.remove("pw_hash");
  p.remove("ap_pass");
  p.remove("ap_off");
  p.end();
}

void apOffLoad() {
  Preferences p; p.begin(NS, true);
  apOff = p.getBool("ap_off", false);
  p.end();
}

void apOffSave() {
  Preferences p; p.begin(NS, false);
  p.putBool("ap_off", apOff);
  p.end();
  uiVersion++;
}

// ------------------------------------------------------------ Office-365-Anmeldung
bool login365Start() {
  if (!linked) {
    login365.st = L_ERR;
    strcpy(login365.msg, "Kein Agent verbunden. Laeuft der Agent auf dem Computer?");
    login365.since = millis();
    uiVersion++;
    return false;
  }
  login365.st = L_REQUESTED;
  login365.code[0] = login365.url[0] = login365.msg[0] = '\0';
  login365.since = millis();
  sendCmd("LOGIN");
  uiVersion++;
  return true;
}

void login365Loop() {
  static uint32_t lastAsk = 0;
  uint32_t now = millis();
  switch (login365.st) {
    case L_REQUESTED:
      if (now - login365.since > 20000) {
        login365.st = L_ERR;
        strcpy(login365.msg, "Der Agent antwortet nicht. Ist er aktuell und gestartet?");
        login365.since = now;
        uiVersion++;
      } else if (now - lastAsk > 3000) {             // Anfrage wiederholen
        lastAsk = now;
        sendCmd("LOGIN");
      }
      break;
    case L_CODE:
      if (now - login365.since > 16UL * 60 * 1000) {  // Device Code laeuft nach 15 min ab
        login365.st = L_ERR;
        strcpy(login365.msg, "Code abgelaufen. Bitte neu starten.");
        login365.since = now;
        uiVersion++;
      }
      break;
    case L_OK:
    case L_ERR:
      if (now - login365.since > 20000) {             // Ergebnis 20 s auf dem Display
        login365.st = L_IDLE;
        uiVersion++;
      }
      break;
    default:
      break;
  }
}
