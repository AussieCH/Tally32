/******************************************************************************
 * lgfx_guition.h  -  GUITION JC1060P470C (ESP32-P4 + ESP32-C6)
 *   7" IPS 1024x600 an MIPI-DSI (2 Lanes, Controller JD9165), Touch GT911.
 *   Init-Sequenz, Timings und Pins aus dem Herstellerpaket
 *   (Demo_Arduino/1_2_Lvgl_V9: esp_lcd_jd9165.c, pins_config.h).
 ******************************************************************************/
#pragma once

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <Wire.h>
#include "gt911.h"

#define TFT_BL  23        // Backlight (Herstellerdemo: nur ein/aus, hier PWM)
#define LCD_RST 27

// --- Panel JD9165 --------------------------------------------------------------
struct Panel_JD9165 : public lgfx::Panel_DSI {
 protected:
  // Format: Laenge (Befehl + Parameter), Befehl, Parameter ... ; 0 = Ende der Liste
  const uint8_t* getInitParams(size_t listno) const override {
    static constexpr uint8_t list0[] = {
      2, 0x30, 0x00,
      5, 0xF7, 0x49, 0x61, 0x02, 0x00,
      2, 0x30, 0x01,
      2, 0x04, 0x0C,
      2, 0x05, 0x00,
      2, 0x06, 0x00,
      2, 0x0B, 0x11,
      2, 0x17, 0x00,
      2, 0x20, 0x04,
      2, 0x1F, 0x05,
      2, 0x23, 0x00,
      2, 0x25, 0x19,
      2, 0x28, 0x18,
      2, 0x29, 0x04,
      2, 0x2A, 0x01,
      2, 0x2B, 0x04,
      2, 0x2C, 0x01,
      2, 0x30, 0x02,
      2, 0x01, 0x22,
      2, 0x03, 0x12,
      2, 0x04, 0x00,
      2, 0x05, 0x64,
      2, 0x0A, 0x08,
      12, 0x0B, 0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x08, 0x1F, 0x1D,
      12, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
      12, 0x0D, 0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x09, 0x1E, 0x1C,
      12, 0x0E, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
      12, 0x0F, 0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1C, 0x1E, 0x09, 0x07,
      12, 0x10, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
      12, 0x11, 0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1D, 0x1F, 0x08, 0x06,
      12, 0x12, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
      5, 0x14, 0x00, 0x00, 0x11, 0x11,
      2, 0x18, 0x99,
      2, 0x30, 0x06,
      15, 0x12, 0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29,
      15, 0x13, 0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29,
      2, 0x30, 0x0A,
      2, 0x02, 0x4F,
      2, 0x0B, 0x40,
      2, 0x12, 0x3E,
      2, 0x13, 0x78,
      2, 0x30, 0x0D,
      2, 0x0D, 0x04,
      2, 0x10, 0x0C,
      2, 0x11, 0x0C,
      2, 0x12, 0x0C,
      2, 0x13, 0x0C,
      2, 0x30, 0x00,
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
      case 1: return 50;                     // nach Display On
      default: return 0;
    }
  }
};

// --- LovyanGFX-Geraet ------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
 public:
  lgfx::Bus_DSI _bus_instance;
  Panel_JD9165  _panel_instance;

  LGFX(void) {
    { // MIPI-DSI-Bus: 2 Lanes, PHY-Versorgung ueber internen LDO 3 mit 2,5 V
      auto cfg = _bus_instance.config();
      cfg.lane_num = 2;
      cfg.lane_mbps = 550;
      cfg.ldo_chan_id = 3;
      cfg.ldo_voltage_mv = 2500;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    { // Panel
      auto cfg = _panel_instance.config();
      cfg.memory_width = 1024; cfg.memory_height = 600;
      cfg.panel_width  = 1024; cfg.panel_height  = 600;
      cfg.offset_x = 0; cfg.offset_y = 0;
      cfg.pin_rst = LCD_RST;
      cfg.rgb_order = true;                  // RGB wie im Herstellertreiber (MADCTL 0)
      _panel_instance.config(cfg);
    }
    { // DPI-Timing (60 Hz): 56 MHz, H 40/160/160, V 10/23/12
      auto det = _panel_instance.config_detail();
      det.dpi_freq_mhz = 56;
      det.hsync_pulse_width = 40; det.hsync_back_porch = 160; det.hsync_front_porch = 160;
      det.vsync_pulse_width = 10; det.vsync_back_porch = 23;  det.vsync_front_porch = 12;
      _panel_instance.config_detail(det);
    }
    setPanel(&_panel_instance);
  }
};

// --- Touch GT911 (I2C SDA=7 / SCL=8, RST/INT nicht verdrahtet) --------------------
#define TOUCH_GT911_SDA 7
#define TOUCH_GT911_SCL 8

inline LGFX lcd;

inline int board_touch_read(GtPoint* pts, int maxp) { return gt911_read(pts, maxp); }

// --- Komplett-Init (gleiche API wie lgfx_crowpanel.h) --------------------------
inline void board_init() {
  Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL);
  Wire.setClock(400000);
  gt911_detect();

  lcd.begin();
  lcd.setRotation(0);                          // quer: 1024 breit x 600 hoch
  lcd.fillScreen(TFT_BLACK);

  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 255);
}

// Display 0/180 Grad quer (flip=true -> Rotation 2).
inline bool g_flip180 = false;
inline void board_set_rotation(bool flip) {
  g_flip180 = flip;
  lcd.setRotation(flip ? 2 : 0);
}
