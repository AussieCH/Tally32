"""
make_walker.py - Ampelmaennchen (gehend) als Alpha-Maske fuer das Symbol NICHT AM PLATZ.

Quelle: Logos/Ampelmaennchen_Geh.png, Wikimedia Commons "Ampelmännchen Geh.png",
Urheber Ludki (de.wikipedia), Lizenz CC BY-SA 2.0 DE. Verwendet wird nur die Figur:
Der Kreis wird entfernt (groesste zusammenhaengende dunkle Flaeche neben der Figur),
die Figur auf Symbolgroesse verkleinert. Ergebnis firmware/src/walker_icon.h (gleiche Lizenz).

Aufruf: ~/.pdf-venv/bin/python tools/make_walker.py
"""
import os
from collections import deque
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "Logos", "Ampelmaennchen_Geh.png")
OUT = os.path.join(ROOT, "firmware", "src", "walker_icon.h")
SIZES = {"7": 86, "10": 114}          # Hoehe in Pixel wie die anderen Symbole (is(190))


def figure_mask(img):
    g = img.convert("L")
    w, h = g.size
    px = g.load()
    dark = [[px[x, y] < 128 for x in range(w)] for y in range(h)]
    seen = [[False] * w for _ in range(h)]
    comps = []
    for y in range(h):
        for x in range(w):
            if dark[y][x] and not seen[y][x]:
                q, pts = deque([(x, y)]), []
                seen[y][x] = True
                while q:
                    cx, cy = q.popleft()
                    pts.append((cx, cy))
                    for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                        if 0 <= nx < w and 0 <= ny < h and dark[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            q.append((nx, ny))
                comps.append(pts)
    comps.sort(key=len, reverse=True)
    # Kreis: umspannt fast das ganze Bild. Figur: groesste Flaeche, die das nicht tut.
    def span(p):
        xs = [a for a, _ in p]; ys = [b for _, b in p]
        return min(xs), min(ys), max(xs), max(ys)
    fig = next(p for p in comps if (span(p)[2] - span(p)[0]) < w * 0.8)
    x0, y0, x1, y1 = span(fig)
    m = Image.new("L", (x1 - x0 + 1, y1 - y0 + 1), 0)
    mp = m.load()
    for x, y in fig:
        mp[x - x0, y - y0] = 255
    return m


def main():
    m = figure_mask(Image.open(SRC))
    out = ["// Erzeugt von tools/make_walker.py, nicht von Hand aendern.",
           "// Ampelmaennchen nach Wikimedia Commons \"Ampelmännchen Geh.png\" von Ludki (de.wikipedia),",
           "// CC BY-SA 2.0 DE (https://creativecommons.org/licenses/by-sa/2.0/de/). Geaendert: Kreis entfernt,",
           "// verkleinert, als Alpha-Maske. Diese Datei steht unter derselben Lizenz.",
           "#pragma once", "#include <stdint.h>", ""]
    for i, (name, hgt) in enumerate(SIZES.items()):
        wid = round(m.width * hgt / m.height)
        a = m.resize((wid, hgt), Image.LANCZOS)
        data = list(a.tobytes())
        out.append("#else" if i else "#if !defined(BOARD_JC8012)")
        out.append(f"static const int WALKER_W = {wid}, WALKER_H = {hgt};   // {name} Zoll")
        out.append("static const uint8_t WALKER_A[] = {")
        for k in range(0, len(data), 24):
            out.append("  " + ",".join(str(v) for v in data[k:k + 24]) + ",")
        out.append("};")
    out.append("#endif")
    open(OUT, "w").write("\n".join(out) + "\n")
    print(f"{OUT}: Figur {m.size}, Groessen {SIZES}")


if __name__ == "__main__":
    main()
