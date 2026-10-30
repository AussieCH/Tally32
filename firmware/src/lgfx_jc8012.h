/******************************************************************************
 * lgfx_jc8012.h  -  GUITION JC8012P4A1C (ESP32-P4 v3 + ESP32-C6), 10,1"
 *   IPS 800x1280 hochkant an MIPI-DSI (2 Lanes, Controller JD9365), quer betrieben
 *   (Rotation 1 = 1280x800). Touch GSL3680 (gsl3680.h), Pins wie JC1060P470C:
 *   LCD-Reset 27, Backlight 23 (PWM), I2C 7/8 (Touch, Echtzeituhr RX8025T).
 *   Das Panel gibt es in zwei Glas-Versionen mit gleichem Controller. Ab Charge 2624
 *   (Etikett hinten, unser Geraet: 2641) ist es V2: Init-Tabelle und Timings aus dem
 *   Herstellerpaket JC8012P4A1C_I_W_Y (uebernommen ueber ESPHome mipi_dsi/guition.py,
 *   Modell JC8012P4A1-V2). V1 stuende im Herstellerdemo esp_lcd_jd9365.c.
 ******************************************************************************/
#pragma once

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <Wire.h>
#include "gsl3680.h"

#define TFT_BL  23        // Backlight, Enable des Boost-Wandlers MP3202, PWM moeglich
#define LCD_RST 27
#define TOUCH_SDA 7
#define TOUCH_SCL 8
#define TOUCH_RST 22
#define TOUCH_INT 21

// --- Panel JD9365 (V2) ----------------------------------------------------------
struct Panel_JD9365 : public lgfx::Panel_DSI {
 protected:
  // Format: Laenge (Befehl + Parameter), Befehl, Parameter ... ; 0 = Ende der Liste.
  // E0 xx waehlt die Registerseite des JD9365.
  const uint8_t* getInitParams(size_t listno) const override {
    static constexpr uint8_t list0[] = {
      2, 0xE0, 0x00, 2, 0xE1, 0x93, 2, 0xE2, 0x65, 2, 0xE3, 0xF8, 2, 0x80, 0x01, 2, 0xE0, 0x01,
      2, 0x00, 0x00, 2, 0x01, 0x44, 2, 0x03, 0x10, 2, 0x04, 0x38, 2, 0x0C, 0x74, 2, 0x17, 0x00,
      2, 0x18, 0xAF, 2, 0x19, 0x00, 2, 0x1A, 0x00, 2, 0x1B, 0xAF, 2, 0x1C, 0x00, 2, 0x35, 0x26,
      2, 0x37, 0x09, 2, 0x38, 0x04, 2, 0x39, 0x00, 2, 0x3A, 0x01, 2, 0x3C, 0x78, 2, 0x3D, 0xFF,
      2, 0x3E, 0xFF, 2, 0x3F, 0x7F, 2, 0x40, 0x06, 2, 0x41, 0xA0, 2, 0x42, 0x81, 2, 0x43, 0x1E,
      2, 0x44, 0x0D, 2, 0x45, 0x28, 2, 0x55, 0x02, 2, 0x57, 0x69, 2, 0x59, 0x0A, 2, 0x5A, 0x2A,
      2, 0x5B, 0x17, 2, 0x5D, 0x7F, 2, 0x5E, 0x6B, 2, 0x5F, 0x5C, 2, 0x60, 0x50, 2, 0x61, 0x4C,
      2, 0x62, 0x3E, 2, 0x63, 0x41, 2, 0x64, 0x2B, 2, 0x65, 0x43, 2, 0x66, 0x42, 2, 0x67, 0x43,
      2, 0x68, 0x62, 2, 0x69, 0x52, 2, 0x6A, 0x5A, 2, 0x6B, 0x4C, 2, 0x6C, 0x48, 2, 0x6D, 0x3A,
      2, 0x6E, 0x28, 2, 0x6F, 0x10, 2, 0x70, 0x7F, 2, 0x71, 0x6B, 2, 0x72, 0x5C, 2, 0x73, 0x50,
      2, 0x74, 0x4C, 2, 0x75, 0x3E, 2, 0x76, 0x41, 2, 0x77, 0x2B, 2, 0x78, 0x43, 2, 0x79, 0x42,
      2, 0x7A, 0x43, 2, 0x7B, 0x62, 2, 0x7C, 0x52, 2, 0x7D, 0x5A, 2, 0x7E, 0x4C, 2, 0x7F, 0x48,
      2, 0x80, 0x3A, 2, 0x81, 0x28, 2, 0x82, 0x10, 2, 0xE0, 0x02, 2, 0x00, 0x42, 2, 0x01, 0x42,
      2, 0x02, 0x40, 2, 0x03, 0x40, 2, 0x04, 0x5E, 2, 0x05, 0x5E, 2, 0x06, 0x5F, 2, 0x07, 0x5F,
      2, 0x08, 0x5F, 2, 0x09, 0x57, 2, 0x0A, 0x57, 2, 0x0B, 0x77, 2, 0x0C, 0x77, 2, 0x0D, 0x47,
      2, 0x0E, 0x47, 2, 0x0F, 0x45, 2, 0x10, 0x45, 2, 0x11, 0x4B, 2, 0x12, 0x4B, 2, 0x13, 0x49,
      2, 0x14, 0x49, 2, 0x15, 0x5F, 2, 0x16, 0x41, 2, 0x17, 0x41, 2, 0x18, 0x40, 2, 0x19, 0x40,
      2, 0x1A, 0x5E, 2, 0x1B, 0x5E, 2, 0x1C, 0x5F, 2, 0x1D, 0x5F, 2, 0x1E, 0x5F, 2, 0x1F, 0x57,
      2, 0x20, 0x57, 2, 0x21, 0x77, 2, 0x22, 0x77, 2, 0x23, 0x46, 2, 0x24, 0x46, 2, 0x25, 0x44,
      2, 0x26, 0x44, 2, 0x27, 0x4A, 2, 0x28, 0x4A, 2, 0x29, 0x48, 2, 0x2A, 0x48, 2, 0x2B, 0x5F,
      2, 0x2C, 0x01, 2, 0x2D, 0x01, 2, 0x2E, 0x00, 2, 0x2F, 0x00, 2, 0x30, 0x1F, 2, 0x31, 0x1F,
      2, 0x32, 0x1E, 2, 0x33, 0x1E, 2, 0x34, 0x1F, 2, 0x35, 0x17, 2, 0x36, 0x17, 2, 0x37, 0x37,
      2, 0x38, 0x37, 2, 0x39, 0x08, 2, 0x3A, 0x08, 2, 0x3B, 0x0A, 2, 0x3C, 0x0A, 2, 0x3D, 0x04,
      2, 0x3E, 0x04, 2, 0x3F, 0x06, 2, 0x40, 0x06, 2, 0x41, 0x1F, 2, 0x42, 0x02, 2, 0x43, 0x02,
      2, 0x44, 0x00, 2, 0x45, 0x00, 2, 0x46, 0x1F, 2, 0x47, 0x1F, 2, 0x48, 0x1E, 2, 0x49, 0x1E,
      2, 0x4A, 0x1F, 2, 0x4B, 0x17, 2, 0x4C, 0x17, 2, 0x4D, 0x37, 2, 0x4E, 0x37, 2, 0x4F, 0x09,
      2, 0x50, 0x09, 2, 0x51, 0x0B, 2, 0x52, 0x0B, 2, 0x53, 0x05, 2, 0x54, 0x05, 2, 0x55, 0x07,
      2, 0x56, 0x07, 2, 0x57, 0x1F, 2, 0x58, 0x40, 2, 0x5B, 0x30, 2, 0x5C, 0x00, 2, 0x5D, 0x34,
      2, 0x5E, 0x05, 2, 0x5F, 0x02, 2, 0x63, 0x00, 2, 0x64, 0x6A, 2, 0x67, 0x73, 2, 0x68, 0x07,
      2, 0x69, 0x08, 2, 0x6A, 0x6A, 2, 0x6B, 0x08, 2, 0x6C, 0x00, 2, 0x6D, 0x00, 2, 0x6E, 0x00,
      2, 0x6F, 0x88, 2, 0x75, 0xFF, 2, 0x77, 0xDD, 2, 0x78, 0x2C, 2, 0x79, 0x15, 2, 0x7A, 0x17,
      2, 0x7D, 0x14, 2, 0x7E, 0x82, 2, 0xE0, 0x04, 2, 0x00, 0x0E, 2, 0x02, 0xB3, 2, 0x09, 0x60,
      2, 0x0E, 0x48, 2, 0x37, 0x58, 2, 0x2B, 0x0F, 2, 0xE0, 0x05, 2, 0x15, 0x1D, 2, 0xE0, 0x00,
      2, 0xE6, 0x02, 2, 0xE7, 0x0C,      2, 0x36, 0x00,                       // MADCTL (Seite 0)
      2, 0x3A, 0x55,                       // RGB565
      1, CMD_SLPOUT,
      0,
    };
    static constexpr uint8_t list1[] = {
      1, CMD_DISPON,
      0,
    };
    switch (listno) {
      case 0: return list0;
      case 1: return list1;
      default: return nullptr;
    }
  }
  size_t getInitDelay(size_t listno) const override {
    switch (listno) {
      case 0: return 120;                    // nach Sleep Out
      case 1: return 20;                     // nach Display On
      default: return 0;
    }
  }
};

// --- LovyanGFX-Geraet ------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
 public:
  lgfx::Bus_DSI _bus_instance;
  Panel_JD9365  _panel_instance;

  LGFX(void) {
    { // MIPI-DSI-Bus: 2 Lanes, 1500 Mbit/s, PHY ueber internen LDO 3 mit 2,5 V
      auto cfg = _bus_instance.config();
      cfg.lane_num = 2;
      cfg.lane_mbps = 1500;
      cfg.ldo_chan_id = 3;
      cfg.ldo_voltage_mv = 2500;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    { // Panel hochkant 800x1280
      auto cfg = _panel_instance.config();
      cfg.memory_width = 800;  cfg.memory_height = 1280;
      cfg.panel_width  = 800;  cfg.panel_height  = 1280;
      cfg.offset_x = 0; cfg.offset_y = 0;
      cfg.pin_rst = LCD_RST;
      cfg.rgb_order = true;
      _panel_instance.config(cfg);
    }
    { // DPI-Timing V2: 70 MHz, H 20/20/40, V 4/10/20
      auto det = _panel_instance.config_detail();
      det.dpi_freq_mhz = 70;
      det.hsync_pulse_width = 20; det.hsync_back_porch = 20; det.hsync_front_porch = 40;
      det.vsync_pulse_width = 4;  det.vsync_back_porch = 10; det.vsync_front_porch = 20;
      _panel_instance.config_detail(det);
    }
    setPanel(&_panel_instance);
  }
};

inline LGFX lcd;

// Touch GSL3680: Rohwerte hochkant (Kalibrierung folgt, bis dahin unveraendert)
// Der GSL3680 liefert schon quer (wie das Bild in Rotation 1), aber in eigenen
// Einheiten: am 30.10.2026 an den vier Ecken gemessen X 25..1627, Y 21..879 bei
// gleichem Abstand zum Rand -> volle Flaeche 1653 x 903. Hier auf Pixel umgerechnet.
// Loslassen entprellt: Der Chip meldet waehrend eines Drucks gelegentlich kurz keinen
// Finger. Erst nach 40 ms ohne Finger gilt er als losgelassen.
inline constexpr int GSL_RAW_W = 1653, GSL_RAW_H = 903;

inline int board_touch_read(GtPoint* pts, int maxp) {
  if (!gsl_ok) { gsl_keep(); return -1; }
  static GtPoint last[2];
  static int lastN = 0;
  static uint32_t zeroSince = 0;
  int n = gsl_read_raw(pts, maxp);
  if (n < 0) return -1;
  for (int i = 0; i < n; i++) {                  // Einheiten des Chips -> Pixel (1280 x 800)
    pts[i].x = constrain(pts[i].x * 1280 / GSL_RAW_W, 0, 1279);
    pts[i].y = constrain(pts[i].y * 800 / GSL_RAW_H, 0, 799);
  }
  if (n > 0) {
    zeroSince = 0;
    lastN = n < 2 ? n : 2;
    for (int i = 0; i < lastN; i++) last[i] = pts[i];
    return n;
  }
  if (lastN && !zeroSince) zeroSince = millis();
  if (lastN && millis() - zeroSince < 40) {      // noch gedrueckt: letzte Position melden
    for (int i = 0; i < lastN && i < maxp; i++) pts[i] = last[i];
    return lastN;
  }
  lastN = 0;
  return 0;
}

// --- Komplett-Init (gleiche API wie lgfx_guition.h) ----------------------------
inline void board_init() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);

  lcd.begin();
  lcd.setRotation(1);                          // quer: 1280 breit x 800 hoch
  lcd.fillScreen(TFT_BLACK);
  delay(50);
  gsl_begin(TOUCH_RST, TOUCH_INT);              // nach dem Panel: Touch sitzt auf dessen Flexkabel

  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 255);
}

// Display 0/180 Grad quer (flip=true -> Rotation 3).
inline bool g_flip180 = false;
inline void board_set_rotation(bool flip) {
  g_flip180 = flip;
  lcd.setRotation(flip ? 3 : 1);
}
