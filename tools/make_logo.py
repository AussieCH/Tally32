#!/usr/bin/env python3
"""
make_logo.py - Logo fuer den Bootscreen aufbereiten: zuschneiden, auf die
Zielbreite skalieren, auf Weiss legen und als PNG-Array in einen C-Header
schreiben. Das Display dekodiert das PNG beim Start (LovyanGFX drawPng).

Aufruf (aus dem Projektordner):
  python3 tools/make_logo.py Logos/Tally32_logo.png firmware/src/logo_png.h 620
  python3 tools/make_logo.py Logos/Tally32_logo.png firmware/src/logo_small_rgba.h 120 LOGO_SMALL --rgba
  python3 tools/make_logo.py Logos/Tally32_logo.png firmware/src/logo_saver_rgba.h 300 LOGO_SAVER --rgba

Mit --rgba entsteht statt PNG ein Rohbild mit Alphakanal (je Pixel R, G, B, A),
das die Firmware selbst mit dem jeweiligen Hintergrund mischt.
"""
import io
import sys

from PIL import Image


def main():
    args = [a for a in sys.argv[1:] if a != "--rgba"]
    rgba = "--rgba" in sys.argv
    if len(args) not in (3, 4):
        sys.exit(__doc__)
    src, header, width = args[0], args[1], int(args[2])
    name = args[3] if len(args) == 4 else "LOGO"
    im = Image.open(src).convert("RGBA")
    im = im.crop(im.getchannel("A").getbbox())
    h = round(im.height * width / im.width)
    im = im.resize((width, h), Image.LANCZOS)
    if rgba:
        data = im.tobytes()                        # R, G, B, A je Pixel
    else:
        bg = Image.new("RGB", im.size, (255, 255, 255))
        bg.paste(im, mask=im.getchannel("A"))
        out = io.BytesIO()
        bg.save(out, "PNG", optimize=True)
        data = out.getvalue()
    lines = ["// Automatisch erzeugt von tools/make_logo.py, nicht von Hand aendern.",
             f"// Quelle: {src}", "#pragma once", "#include <Arduino.h>", "",
             f"constexpr int {name}_W = {width};", f"constexpr int {name}_H = {h};",
             f"static const uint8_t {name}_{'RGBA' if rgba else 'PNG'}[{len(data)}] PROGMEM = {{"]
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    lines += ["};", ""]
    with open(header, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"{width}x{h}, {len(data)} Bytes")


if __name__ == "__main__":
    main()
