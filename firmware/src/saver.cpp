/******************************************************************************
 * saver.cpp  -  Bildschirmschoner (Rechner gesperrt)
 *
 *   Dunkler Grund im Ton des Logos, das Logo wandert langsam ueber den
 *   Bildschirm und prallt an den Raendern ab. Unten steht "Tap Me" und springt
 *   alle 30 s eine Position weiter, von links nach rechts und zurueck.
 *   Das Logo liegt fertig gemischt in einem Sprite mit Rand in Hintergrundfarbe:
 *   pro Schritt nur ein Blit, der Rand loescht die alte Position (kein Flackern).
 ******************************************************************************/
#include "board.h"
#include "logo_saver_rgba.h"
#include "state.h"
#include "ui.h"

static const uint32_t SAVER_BG = 0x15111F;      // fast schwarz, leicht violett
static const uint32_t TAP_COL  = 0xB69AF4;      // Lila aus dem Logo
static const int M = 3;                         // Sprite-Rand (> Schrittweite)
static const int SPR_W = LOGO_SAVER_W + 2 * M, SPR_H = LOGO_SAVER_H + 2 * M;
static const int TAP_Y = SCR_H - 34, TAP_N = 5;        // Zeile "Tap Me", 5 Positionen
static const int LOGO_MAX_Y = TAP_Y - 34 - SPR_H;
static const uint32_t STEP_MS = 40, TAP_MS = 30000;

static LGFX_Sprite spr(&lcd);
static bool active = false, sprReady = false;
static int px = 120, py = 80, dx = 1, dy = 1;
static int tapIdx = 0, tapDir = 1;
static uint32_t nextStep = 0, nextTap = 0;

static void buildSprite() {
  spr.setColorDepth(16);
  spr.setPsram(true);
  if (!spr.createSprite(SPR_W, SPR_H)) return;
  spr.fillScreen(c565(SAVER_BG));
  const uint8_t* p = LOGO_SAVER_RGBA;
  for (int y = 0; y < LOGO_SAVER_H; y++)
    for (int x = 0; x < LOGO_SAVER_W; x++, p += 4) {
      uint8_t a = pgm_read_byte(p + 3);
      if (!a) continue;
      uint32_t fg = ((uint32_t)pgm_read_byte(p) << 16) | ((uint32_t)pgm_read_byte(p + 1) << 8) | pgm_read_byte(p + 2);
      spr.drawPixel(M + x, M + y, c565(mixRGB(SAVER_BG, fg, a * 100 / 255)));
    }
  sprReady = true;
}

static int tapX(int i) { return 100 + i * (SCR_W - 200) / (TAP_N - 1); }

static void drawTap(int i) {
  lcd.setFont(smooth(26));
  lcd.setTextColor(c565(TAP_COL), c565(SAVER_BG));
  lcd.setTextDatum(middle_center);
  lcd.drawString("Tap Me", tapX(i), TAP_Y);
  lcd.setTextDatum(top_left);
}

bool saverActive() { return active; }

void saverStart() {
  if (active) return;
  if (!sprReady) buildSprite();
  active = true;
  lcd.fillScreen(c565(SAVER_BG));
  if (sprReady) spr.pushSprite(px, py);
  drawTap(tapIdx);
  nextStep = millis() + STEP_MS;
  nextTap = millis() + TAP_MS;
}

void saverStop() {
  if (!active) return;
  active = false;
  uiVersion++;                                   // normale Anzeige neu zeichnen
}

void saverLoop() {
  if (!active) return;
  uint32_t now = millis();
  if (sprReady && now >= nextStep) {
    nextStep = now + STEP_MS;
    px += dx; py += dy;
    if (px <= 0 || px >= SCR_W - SPR_W) { dx = -dx; px = constrain(px, 0, SCR_W - SPR_W); }
    if (py <= 0 || py >= LOGO_MAX_Y)    { dy = -dy; py = constrain(py, 0, LOGO_MAX_Y); }
    spr.pushSprite(px, py);
  }
  if (now >= nextTap) {                          // "Tap Me" springt weiter
    nextTap = now + TAP_MS;
    lcd.fillRect(0, TAP_Y - 24, SCR_W, 48, c565(SAVER_BG));
    if (tapIdx + tapDir < 0 || tapIdx + tapDir >= TAP_N) tapDir = -tapDir;
    tapIdx += tapDir;
    drawTap(tapIdx);
  }
}
