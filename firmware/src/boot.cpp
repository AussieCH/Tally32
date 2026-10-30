/******************************************************************************
 * boot.cpp  -  Bootscreen mit dem Tally32-Logo
 *   Weisser Grund, Logo (PNG aus logo_png.h, erzeugt mit tools/make_logo.py),
 *   Untertitel, Version und Fortschrittsbalken in den Logofarben.
 ******************************************************************************/
#include "board.h"
#include "logo_png.h"
#include "state.h"
#include "ui.h"

static const uint32_t PURPLE = 0x432078, LILAC = 0xB69AF4, WHITE = 0xFFFFFF;
static const int BAR_W = lx(400), BAR_X = (SCR_W - BAR_W) / 2, BAR_Y = ly(400), BAR_H = 8;

void bootScreen() {
  lcd.fillScreen(c565(WHITE));
  lcd.drawPng(LOGO_PNG, sizeof(LOGO_PNG), (SCR_W - LOGO_W) / 2, ly(96));
  lcd.setFont(smooth(26));
  lcd.setTextColor(c565(mixRGB(PURPLE, WHITE, 25)), c565(WHITE));
  lcd.setTextDatum(middle_center);
  lcd.drawString(TR("Statusanzeige f\xC3\xBCr den Arbeitsplatz", "Workplace status display"), SCR_W / 2, ly(330));
  lcd.setFont(smooth(20));
  lcd.setTextColor(c565(mixRGB(PURPLE, WHITE, 45)), c565(WHITE));
  lcd.drawString("Version " TALLY32_VERSION " " TALLY32_EDITION, SCR_W / 2, ly(446));
  lcd.setTextDatum(top_left);
  lcd.fillRoundRect(BAR_X, BAR_Y, BAR_W, BAR_H, BAR_H / 2, c565(mixRGB(LILAC, WHITE, 70)));
}

// pct 0..100
void bootProgress(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int w = BAR_W * pct / 100;
  if (w >= BAR_H) lcd.fillRoundRect(BAR_X, BAR_Y, w, BAR_H, BAR_H / 2, c565(PURPLE));
}
