/******************************************************************************
 * ui.h  -  Zeichenhilfen, die Statusanzeige (main.cpp) und Buchung
 *          (booking.cpp) gemeinsam nutzen
 ******************************************************************************/
#pragma once
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <time.h>

#if defined(BOARD_JC8012)
constexpr int SCR_W = 1280;                     // GUITION JC8012P4A1C, quer
constexpr int SCR_H = 800;
#else
constexpr int SCR_W = 1024;                     // GUITION JC1060P470C
constexpr int SCR_H = 600;
#endif
// Das Layout ist in 800x480 gedacht: lx()/ly() verteilen es auf die volle Flaeche.
constexpr int lx(int x) { return x * SCR_W / 800; }
constexpr int ly(int y) { return y * SCR_H / 480; }
// Feste Masse des 7"-Layouts (1024x600) auf das aktuelle Board: ux waagrecht (10": 1,25),
// uy senkrecht und us fuer Groessen wie Symbole, Radien, Linien (10": 1,33). Auf 7" unveraendert.
constexpr int ux(int x) { return x * SCR_W / 1024; }
constexpr int uy(int y) { return y * SCR_H / 600; }
constexpr int us(int v) { return v * SCR_H / 600; }

// Zweisprachige Texte: TR("deutsch", "english") waehlt nach uiLang (Menue "Sprache / Language").
// Nur zur Laufzeit verwenden, nicht in statischen Initialisierungen.
extern uint8_t uiLang;
#define TR(de, en) (uiLang ? (en) : (de))   // nicht "T": kollidiert mit Template-Parametern

struct Theme { uint16_t bg, fg, dim; };
Theme themeFrom(uint32_t bg);                   // Schrift passend zur Helligkeit
uint16_t c565(uint32_t c);
uint16_t colLeft();
uint16_t colInk();
uint32_t mixRGB(uint32_t a, uint32_t b, int pctB);   // a -> b um pctB Prozent
uint32_t inkRGB();                              // Schrift-/Symbolfarbe der linken Kachel
const lgfx::IFont* smooth(int size);            // glatte Schrift: 20, 26, 34, 48 px, 100 = nur Ziffern


// Bootscreen (boot.cpp)
#define TALLY32_VERSION "1.1"
#define TALLY32_EDITION "Basic"             // Client-Typ, steht neben der Version
void bootScreen();
void bootProgress(int pct);

// Terminbuchung am Display (booking.cpp)
bool bookingActive();
void bookingOpen();
void bookingTap(int x, int y);
void bookingLoop();
bool bookingSerial(char* line);
void bookingCancel();                           // Dialog schliessen (z. B. Notebook weg)
void bookingDemoStart();                        // Buchung automatisch vorfuehren (Demomodus)
// Allgemeine Texteingabe mit der Buchungstastatur (z. B. Name im Menue). Schreibt bei
// "Fertig" nach buf (hoechstens max Bytes inkl. Null) und ruft done(true), bei
// "Abbrechen" done(false). Laeuft wie die Buchung als eigener Vollbild-Dialog.
void textEntryOpen(const char* title, char* buf, size_t max, void (*done)(bool ok));

// Einstellungsmenue am Geraet (menu.cpp)
bool menuActive();
void menuOpen();                                // 3 s auf die Plakette: PIN, dann Menue
void menuTap(int x, int y);
void menuLoop();

// Bildschirmschoner (saver.cpp)
bool saverActive();
void saverStart();
void saverStop();
void saverLoop();

// Ortszeit: echte Uhrzeit vom Agent, sonst (nur Demo) eine Ersatzuhr ab Montag 10:00
void localNow(struct tm& t);
const char* shotDateText();                      // Demo-Datum fuer Bildschirmfotos (USB DATE), "" = keins
time_t localEpoch();                            // dieselbe Ortszeit als Sekunden (gmtime_r-tauglich)                 // true = Zeile gehoerte zur Buchung
