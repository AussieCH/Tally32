/******************************************************************************
 * gt911.h  -  minimaler Poller fuer den kapazitiven Touch-Controller GT911
 *   (CrowPanel und GUITION). Nutzt die Werks-Config, schreibt nichts ausser
 *   dem Statusbit. Wire muss vorher auf den richtigen Pins laufen.
 ******************************************************************************/
#pragma once
#include <Arduino.h>
#include <Wire.h>

inline uint8_t gt911_addr = 0x5D;          // wird in gt911_detect() bestaetigt

// 16-bit-Register, Big-Endian. Punkt 1 ab 0x8150: id, xL, xH, yL, yH, ...
inline uint8_t gt911_read8(uint16_t reg) {
  Wire.beginTransmission(gt911_addr);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  if (Wire.endTransmission() != 0) return 0;
  Wire.requestFrom(gt911_addr, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}
inline void gt911_readN(uint16_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(gt911_addr);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  if (Wire.endTransmission() != 0) { for (int i = 0; i < n; i++) buf[i] = 0; return; }
  Wire.requestFrom(gt911_addr, n);
  for (int i = 0; i < n; i++) buf[i] = Wire.available() ? Wire.read() : 0;
}
inline void gt911_write8(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(gt911_addr);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF); Wire.write(v);
  Wire.endTransmission();
}

// GT911-Adresse bestaetigen (0x5D bei INT=low beim Reset, sonst 0x14)
inline void gt911_detect() {
  Wire.beginTransmission(0x5D);
  if (Wire.endTransmission() == 0) gt911_addr = 0x5D;
  else {
    Wire.beginTransmission(0x14);
    if (Wire.endTransmission() == 0) gt911_addr = 0x14;
  }
  gt911_write8(0x814E, 0);                     // evtl. anstehendes Statusbit quittieren
}

struct GtPoint { int x; int y; };

inline uint8_t gt911_raw0[8];               // Rohbytes von Punkt 1 (Debug)

// Liest aktuelle Beruehrungen (raw, Panel-nativ).
//  Return >= 0 : frisches Sample mit dieser Anzahl Beruehrungen
//  Return  < 0 : KEIN frisches Sample -> Aufrufer haelt den letzten Zustand
//                (sonst flackert eine gehaltene Beruehrung pressed/released!)
inline int gt911_read(GtPoint* pts, int maxp) {
  uint8_t status = gt911_read8(0x814E);
  if (!(status & 0x80)) return -1;         // kein neues Datenpaket bereit
  int n = status & 0x0F;
  if (n > maxp) n = maxp;
  for (int i = 0; i < n; i++) {
    uint8_t b[8];
    gt911_readN(0x8150 + i * 8, b, 8);
    if (i == 0) for (int k = 0; k < 8; k++) gt911_raw0[k] = b[k];
    // Punkt 1 ab 0x8150: Xlow, Xhigh, Ylow, Yhigh (Track-ID liegt davor bei 0x814F)
    pts[i].x = b[0] | (b[1] << 8);
    pts[i].y = b[2] | (b[3] << 8);
  }
  gt911_write8(0x814E, 0);                  // Statusbit quittieren
  return n;
}
