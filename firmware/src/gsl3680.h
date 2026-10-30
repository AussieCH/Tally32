/******************************************************************************
 * gsl3680.h  -  Touch-Controller GSL3680 (Silead) des GUITION JC8012P4A1C
 *   Eigener schlanker Treiber ueber Wire (gleicher Bus wie die Echtzeituhr), ohne
 *   den GPL-Algorithmus des Herstellers (gsl_point_id): Der Chip liefert die
 *   Koordinaten selbst, wie beim Linux-Treiber fuer Silead-Chips.
 *   Ablauf: Reset, Register loeschen, Firmware laden (gsl3680_fw_data.h),
 *   starten. Danach ab Register 0x80: Anzahl Finger, je Finger 4 Byte
 *   (Y 16 Bit, X 12 Bit + 4 Bit Finger-ID).
 ******************************************************************************/
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "gt911.h"                               // GtPoint
#include "gsl3680_fw_data.h"

inline constexpr uint8_t GSL_ADDR = 0x40;
inline bool gsl_ok = false;                      // Firmware laeuft (0xB0 = 5A5A5A5A)
inline bool gsl_log = false;                     // Diagnose: Rohbytes ausgeben (SET touchlog 1)

inline bool gsl_write(uint8_t reg, const uint8_t* d, uint8_t n) {
  Wire.beginTransmission(GSL_ADDR);
  Wire.write(reg);
  Wire.write(d, n);
  return Wire.endTransmission() == 0;
}
inline bool gsl_write1(uint8_t reg, uint8_t v) { return gsl_write(reg, &v, 1); }
inline bool gsl_read(uint8_t reg, uint8_t* d, uint8_t n) {
  Wire.beginTransmission(GSL_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(GSL_ADDR, n) != n) return false;
  for (int i = 0; i < n; i++) d[i] = Wire.read();
  return true;
}

inline void gsl_reset(int rst) {
  digitalWrite(rst, LOW);  delay(20);
  digitalWrite(rst, HIGH); delay(20);
  gsl_write1(0xE0, 0x88); delay(10);
  gsl_write1(0xE4, 0x04); delay(10);
  const uint8_t z[4] = { 0, 0, 0, 0 };
  gsl_write(0xBC, z, 4); delay(10);
}

inline void gsl_clear() {
  gsl_write1(0xE0, 0x88); delay(20);
  gsl_write1(0x88, 0x01); delay(5);
  gsl_write1(0xE4, 0x04); delay(5);
  gsl_write1(0xE0, 0x00); delay(20);
}

inline int gsl_load_fw() {                       // -> Anzahl Schreibfehler
  int errors = 0;
  for (const GslFw& e : GSL3680_FW) {
    uint8_t b[4] = { (uint8_t)e.val, (uint8_t)(e.val >> 8), (uint8_t)(e.val >> 16), (uint8_t)(e.val >> 24) };
    if (!gsl_write(e.reg, b, e.reg == 0xF0 ? 1 : 4)) errors++;
  }
  return errors;
}

inline void gsl_start() { gsl_write1(0xE0, 0x00); delay(10); }

// Komplett-Init wie im Herstellerdemo: loeschen, Reset, Firmware, Start, Reset, Start.
// Bei Charge 2641 kommt der Chip gelegentlich nicht hoch (0xB0 bleibt 0): dann bis zu
// dreimal von vorn, mit mehr Wartezeit vor der Pruefung.
inline int gsl_rst_pin = -1;
inline bool gsl_begin_once(int rst, int attempt) {
  gsl_reset(rst);
  gsl_clear();
  gsl_reset(rst);
  int err = gsl_load_fw();
  gsl_start();
  gsl_reset(rst);
  gsl_start();
  uint8_t b[4] = { 0 };
  bool ok = false;
  for (int i = 0; i < 10 && !ok; i++) {          // bis 200 ms auf die laufende Firmware warten
    delay(20);
    ok = gsl_read(0xB0, b, 4) && b[0] == 0x5A && b[1] == 0x5A && b[2] == 0x5A && b[3] == 0x5A;
  }
  Serial.printf("LOG gsl3680 %s, Versuch %d (fw %d Eintraege, %d Fehler, 0xB0=%02x%02x%02x%02x)\n",
                ok ? "ok" : "FEHLER", attempt, (int)(sizeof(GSL3680_FW) / sizeof(GSL3680_FW[0])), err, b[3], b[2], b[1], b[0]);
  return ok;
}

inline bool gsl_begin(int rst, int irq) {
  gsl_rst_pin = rst;
  // Einschalten wie im Herstellerdemo: INT waehrend des Resets als Ausgang auf Low
  // (waehlt Startmodus und Adresse), sonst kommt der Chip je nach Zufall nicht hoch.
  pinMode(rst, OUTPUT);
  pinMode(irq, OUTPUT);
  digitalWrite(rst, LOW);
  digitalWrite(irq, LOW);
  delay(10);
  delay(1);
  digitalWrite(rst, HIGH);
  delay(60);
  uint8_t b[4] = { 0x12, 0x34, 0x56, 0x00 };   // Kontakt aufnehmen wie der Hersteller
  uint8_t r[4];
  gsl_read(0xF0, r, 4);
  gsl_write(0xF0, b, 4);
  delay(20);
  for (int a = 1; a <= 3 && !gsl_ok; a++) gsl_ok = gsl_begin_once(rst, a);
  pinMode(irq, INPUT);                           // danach INT als Eingang
  return gsl_ok;
}

// Im Betrieb: Touch nicht bereit -> alle 10 s erneut starten
inline void gsl_keep() {
  static uint32_t last = 0;
  static int attempt = 3;
  if (gsl_ok || gsl_rst_pin < 0 || millis() - last < 10000) return;
  last = millis();
  gsl_ok = gsl_begin_once(gsl_rst_pin, ++attempt);
}

// Rohkoordinaten des Chips (hochkant, wie das Panel). >= 0: Anzahl Finger.
inline int gsl_read_raw(GtPoint* pts, int maxp) {
  uint8_t d[24];
  if (!gsl_read(0x80, d, sizeof(d))) return -1;
  int n = d[0];
  if (gsl_log && n) {
    static uint32_t last = 0;
    if (millis() - last >= 100) {
      last = millis();
      Serial.printf("LOG gsl n=%d %02x %02x %02x %02x | %02x %02x %02x %02x\n", n, d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11]);
    }
  }
  if (n > 2) n = 2;                              // 24 Byte reichen fuer zwei Finger
  if (n > maxp) n = maxp;
  for (int i = 0; i < n; i++) {
    const uint8_t* p = d + 4 + 4 * i;
    pts[i].y = (p[0] | (p[1] << 8)) & 0x0FFF;    // obere Bits: Status
    pts[i].x = p[2] | ((p[3] & 0x0F) << 8);
  }
  return n;
}
