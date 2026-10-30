/******************************************************************************
 * state.h  -  gemeinsamer Zustand von Anzeige (main.cpp), Einstellungen
 *             (settings.cpp) und Webinterface (web.cpp)
 ******************************************************************************/
#pragma once
#include <Arduino.h>

// M_OUT = nicht am Platz (offline), M_CLOSED = ausserhalb der Arbeitszeit (Feierabend)
enum Mode { M_NONE = -1, M_OFFLINE = 0, M_FREE, M_BUSY, M_AWAY, M_CALL, M_OUT, M_CLOSED };

struct Status {
  Mode mode;
  char clock[8];
  char freeAt[8];
  char nextAt[8];
  char src[24];
  bool offline;                 // aus dem Offline-Puffer berechnet (Notebook weg)
  char stand[8];                // Uhrzeit der letzten Daten vom Agent
  Mode base;                    // bei M_CALL: Zustand darunter (M_FREE/M_BUSY/M_AWAY), sonst 0
};

extern Status cur;              // zuletzt vom Agent gemeldeter Status
extern bool linked;             // Agent meldet sich regelmaessig
extern char account[64];        // angemeldetes Microsoft-Konto (vom Agent), "" = keins
extern bool canWrite;           // Token hat Calendars.ReadWrite (fuer die Terminbuchung)
extern uint32_t uiVersion;      // hochzaehlen -> Display komplett neu zeichnen
extern int rtcState;            // Echtzeituhr: 0 fehlt, 1 Zeit gueltig, 2 Zeit verloren
const char* resetReasonText();  // Grund des letzten Neustarts (Diagnose)

// ------------------------------------------------------------ Farben (RGB888)
struct Colors { uint32_t left, free_, busy, away, call; };
extern Colors colors;
extern const Colors DEFAULT_COLORS;
void colorsLoad();
void colorsSave();

// ------------------------------------------------------------ Anzeige
extern uint8_t brightness;
extern bool flip;
extern uint8_t uiLang;           // Sprache der Oberflaeche: 0 Deutsch, 1 English (NVS "lang")
extern String ownerName;        // Name der Person im Banner oben (leer = Konto vom Agent)
void displayLoad();
void displayApply(bool save);

// ------------------------------------------------------------ Terminbuchung
// Besucher bucht am Display einen Termin (ein/aus im Webinterface).
extern bool bookingEnabled;
extern uint16_t bookFrom, bookTo;   // frueheste / spaeteste Zeit in Minuten ab 00:00
extern uint8_t bookDays;            // Bit 0 = Montag ... Bit 6 = Sonntag
extern bool lunchOn;                // Mittagspause nicht buchbar
extern uint16_t lunchFrom, lunchTo; // Minuten ab 00:00
void bookingLoad();
void bookingSave();

// ------------------------------------------------------------ Demomodus
// Zeigt reihum alle Zustaende und fuehrt die Buchung vor, ohne Agent/Konto.
extern bool demoMode;
void demoSave();

// ------------------------------------------------------------ Datenquelle
// Befehl an den Agenten auf dem Computer (USB-Seriell)
void sendCmd(const char* line);
void langSet(uint8_t l);        // Sprache setzen, speichern, dem Agenten melden (LANG de|en)

// ------------------------------------------------------------ Zugang
// Solange kein eigenes Passwort gespeichert ist, gilt das Standardpasswort
// und muss beim ersten Login geaendert werden.
constexpr const char* WEB_USER        = "admin";
constexpr const char* WEB_DEFAULT_PW  = "tally32";
constexpr const char* AP_DEFAULT_PASS = "tally32setup";
bool authCheck(const String& pw);
bool authMustChange();
void authSet(const String& pw);
String apPassLoad();
void apPassSave(const String& pass);
void accessReset();             // Web-Passwort, WLAN-Passwort und PIN auf Standard, Access Point ein
// PIN fuer das Einstellungsmenue am Geraet (4 Ziffern). 4 Ziffern lassen sich ohnehin
// durchprobieren, deshalb im Klartext im NVS, Schutz kommt von der Sperre nach Fehlversuchen.
constexpr const char* MENU_DEFAULT_PIN = "0000";
String pinLoad();
void pinSave(const String& pin);
bool pinValid(const String& pin);               // genau 4 Ziffern
// Einrichtungs-WLAN (Access Point) abschaltbar. Wieder ein: 5 s aufs Display druecken.
extern bool apOff;
void apOffLoad();
void apOffSave();

// ------------------------------------------------------------ Office-365-Anmeldung
// Ablauf ueber den Agent: Geraet sendet "LOGIN", Agent antwortet mit
// "LC <code> <url>" und spaeter "LR OK <konto>" bzw. "LR ERR <text>".
enum LoginState { L_IDLE, L_REQUESTED, L_CODE, L_OK, L_ERR };
struct Login365 {
  LoginState st;
  char code[24];
  char url[80];
  char msg[96];
  uint32_t since;
};
extern Login365 login365;
bool login365Start();           // false, wenn kein Agent verbunden ist
void login365Loop();            // LOGIN wiederholen, Timeouts

// ------------------------------------------------------------ Web
void webBegin();
void webLoop();
const char* webSsid();
int webApState();                               // 0 aus, 1 laeuft, 2 Start fehlgeschlagen
void apApply();                 // Access Point gemaess apOff starten bzw. stoppen
