/******************************************************************************
 * rtc.h  -  Echtzeituhr RX8025T (GUITION: am I2C-Bus des Touch, Adresse 0x32,
 *           Pufferung ueber eine Knopfzelle CR1220)
 *   Haelt die Uhrzeit ueber einen Stromunterbruch, z. B. wenn das Dock beim
 *   Abdocken kurz den Strom wegnimmt. Gespeichert wird UTC. Ohne Knopfzelle
 *   setzt der Chip nach dem Unterbruch das Flag VLF, die Zeit gilt dann nicht.
 *   Gelesene Werte werden auf Plausibilitaet geprueft, im Zweifel gilt die
 *   Zeit als ungueltig (dann wie ohne Uhr: KEIN SIGNAL bis zum Agent).
 ******************************************************************************/
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <time.h>

enum RtcState { RTC_NONE = 0, RTC_OK = 1, RTC_LOST = 2 };   // fehlt / Zeit gueltig / Zeit verloren

inline constexpr uint8_t RTC_ADDR = 0x32;
inline constexpr uint8_t RTC_REG_FLAG = 0x0E;   // Bit 1 = VLF (Spannung war zu tief)
inline bool rtc_present = false;

inline uint8_t rtc_bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
inline uint8_t rtc_bin2bcd(int v) { return ((v / 10) << 4) | (v % 10); }

// Tage seit 1970-01-01 fuer ein Datum (proleptisch gregorianisch, ohne timegm)
inline int64_t rtc_days(int y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

inline bool rtc_begin() {
  Wire.beginTransmission(RTC_ADDR);
  rtc_present = Wire.endTransmission() == 0;
  return rtc_present;
}

inline RtcState rtc_read(time_t& utc) {
  if (!rtc_present) return RTC_NONE;
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return RTC_NONE;
  uint8_t r[16];
  if (Wire.requestFrom(RTC_ADDR, (uint8_t)16) != 16) return RTC_NONE;
  for (auto& b : r) b = Wire.read();
  if (r[RTC_REG_FLAG] & 0x02) return RTC_LOST;
  int sec = rtc_bcd2bin(r[0] & 0x7F), min = rtc_bcd2bin(r[1] & 0x7F), hour = rtc_bcd2bin(r[2] & 0x3F);
  int day = rtc_bcd2bin(r[4] & 0x3F), mon = rtc_bcd2bin(r[5] & 0x1F), year = 2000 + rtc_bcd2bin(r[6]);
  if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || mon < 1 || mon > 12 || year < 2024)
    return RTC_LOST;
  utc = (time_t)(rtc_days(year, mon, day) * 86400 + hour * 3600 + min * 60 + sec);
  return RTC_OK;
}

inline bool rtc_write(time_t utc) {
  if (!rtc_present) return false;
  struct tm t;
  gmtime_r(&utc, &t);
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  Wire.write(rtc_bin2bcd(t.tm_sec));
  Wire.write(rtc_bin2bcd(t.tm_min));
  Wire.write(rtc_bin2bcd(t.tm_hour));
  Wire.write(1 << t.tm_wday);                     // Wochentag als Bit, Sonntag = Bit 0
  Wire.write(rtc_bin2bcd(t.tm_mday));
  Wire.write(rtc_bin2bcd(t.tm_mon + 1));
  Wire.write(rtc_bin2bcd(t.tm_year % 100));
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(RTC_ADDR);               // VLF loeschen: Zeit ist jetzt gueltig
  Wire.write(RTC_REG_FLAG);
  Wire.write(0x00);
  return Wire.endTransmission() == 0;
}
