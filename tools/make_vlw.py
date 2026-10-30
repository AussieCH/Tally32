#!/usr/bin/env python3
"""
make_vlw.py - erzeugt kantengeglaettete VLW-Fonts (8 bit Alpha) als C-Header
fuer LovyanGFX. Enthaelt ASCII plus Umlaute, damit das Display echte Umlaute
zeichnen kann.

Aufruf (aus dem Projektordner):
  python3 tools/make_vlw.py tools/fonts/Montserrat-Medium.ttf firmware/src/fonts_vlw.h 20 26 34 48 100d

Eine Groesse mit "d" (z. B. 100d) enthaelt nur Ziffern, ":", "." und "-"
(grosse Uhrzeiten, spart Flash).

Format (alle Werte int32 big-endian):
  Kopf: Anzahl, Version 11, Groesse, 0, Ascent, Descent
  je Zeichen: Unicode, Hoehe, Breite, xAdvance, dY (ueber Grundlinie), dX, 0
  danach die Bitmaps (Breite x Hoehe Bytes je Zeichen)
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

CHARS = [chr(c) for c in range(0x20, 0x7F)] + list("ÄÖÜäöüßéèàçâêô«»·…–")
DIGITS = list(" 0123456789:.-")


def vlw(ttf, size, chars=CHARS):
    font = ImageFont.truetype(ttf, size)
    ascent, descent = font.getmetrics()
    metrics, bitmaps = [], []
    for ch in sorted(set(chars), key=ord):
        adv = round(font.getlength(ch))
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        if ch == " " or w == 0 or h == 0:
            metrics.append((ord(ch), 0, 0, adv, 0, 0, 0))
            continue
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        metrics.append((ord(ch), h, w, adv, -y0, x0, 0))
        bitmaps.append(img.tobytes())
    out = struct.pack(">6i", len(metrics), 11, size, 0, ascent, descent)
    for m in metrics:
        out += struct.pack(">7i", *m)
    for b in bitmaps:
        out += b
    return out


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    ttf, header, sizes = sys.argv[1], sys.argv[2], sys.argv[3:]
    lines = ["// Automatisch erzeugt von tools/make_vlw.py, nicht von Hand aendern.",
             f"// Schrift: {ttf.split('/')[-1]} (SIL Open Font License), Groessen {sizes}",
             "#pragma once", "#include <Arduino.h>", ""]
    for spec in sizes:
        digits = spec.endswith("d")
        s = int(spec.rstrip("d"))
        data = vlw(ttf, s, DIGITS if digits else CHARS)
        lines.append(f"static const uint8_t VLW_{s}[{len(data)}] PROGMEM = {{")
        for i in range(0, len(data), 24):
            lines.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append("")
        print(f"Groesse {s}: {len(data)} Bytes")
    with open(header, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
