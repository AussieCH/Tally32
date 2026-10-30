#!/usr/bin/env python3
"""
make_icons.py - Verbindungssymbole (schwarz auf weiss) als Alpha-Masken fuer die Firmware

  python3 tools/make_icons.py [breite]          Standard 48 px

Liest Logos/verbunden.png und Logos/getrennt.png, macht aus Schwarz deckend und aus
Weiss durchsichtig, schneidet beide auf denselben Rahmen zu (gleiche Groesse am Display)
und schreibt firmware/src/icons_conn.h. Die Firmware faerbt die Maske mit der
Schriftfarbe der Kachel ein (drawConnIcon in main.cpp).
"""
import os
import sys

from PIL import Image, ImageChops, ImageOps

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = [("ICON_CONN_ON", "verbunden.png"), ("ICON_CONN_OFF", "getrennt.png")]


def main():
    width = int(sys.argv[1]) if len(sys.argv) > 1 else 48
    masks = [(name, ImageOps.invert(Image.open(os.path.join(ROOT, "Logos", f)).convert("L"))) for name, f in SRC]
    box = None                                   # gemeinsamer Rahmen um beide Symbole
    for _, m in masks:
        b = m.point(lambda v: 255 if v > 40 else 0).getbbox()
        box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    pad = 8
    box = (max(0, box[0] - pad), max(0, box[1] - pad), box[2] + pad, box[3] + pad)
    out = ["// Automatisch erzeugt von tools/make_icons.py, nicht von Hand aendern.",
           "// Alpha-Masken (0 = durchsichtig, 255 = deckend), Zeile fuer Zeile.", "#pragma once",
           "#include <stdint.h>"]
    h = None
    for name, m in masks:
        m = m.crop(box)
        h = round(m.height * width / m.width)
        m = m.resize((width, h), Image.LANCZOS)
        m = ImageChops.multiply(m, Image.new("L", m.size, 255))
        data = m.tobytes()
        out.append(f"static const uint8_t {name}[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            out.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
        out.append("};")
    out.insert(4, f"constexpr int ICON_CONN_W = {width}, ICON_CONN_H = {h};")
    path = os.path.join(ROOT, "firmware", "src", "icons_conn.h")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"{path}: {width} x {h} px")


if __name__ == "__main__":
    main()
