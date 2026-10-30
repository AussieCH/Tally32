#!/usr/bin/env python3
"""
screenshot.py - Bildschirm des Tally32-Displays per USB abholen (Befehl SHOT)

  python3 tools/screenshot.py <port> <datei.png>          ein Bild, so wie es gerade ist
  python3 tools/screenshot.py <port> --demo <ordner>      Demomodus einschalten und alle
                                                          Zustaende plus Buchungsbilder ablegen

Das Oeffnen des Ports startet das Display neu (USB-Serial/JTAG), das Skript wartet
den Start ab. Braucht pyserial und Pillow (z. B. ~/.pdf-venv oder agent/.venv).
"""
import os
import sys
import time

import serial
from PIL import Image

NAMES = {0: "kein-signal", 1: "frei", 2: "besetzt", 3: "abwesend", 4: "on-air", 5: "nicht-am-platz", 6: "ausser-haus"}


def open_port(port):
    """Oeffnet den Port. Das Display startet dabei neu und meldet sich kurz vom USB ab,
    deshalb mit Wiederholung, bis es stabil antwortet."""
    deadline = time.time() + 20
    while True:
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port, 115200, 2
            s.dtr = False
            s.rts = False
            s.open()
            time.sleep(2.5)                       # Neustart abwarten
            s.reset_input_buffer()
            s.write(b"HELLO\n")
            t = time.time()
            while time.time() - t < 4:
                if b"TALLY32" in s.readline():
                    time.sleep(1.0)
                    return s
            s.close()
        except (serial.SerialException, OSError):
            try:
                s.close()
            except Exception:  # noqa: BLE001
                pass
        if time.time() > deadline:
            sys.exit("Display antwortet nicht auf " + port)
        time.sleep(1.0)


def status(s):
    s.reset_input_buffer()
    s.write(b"STATUS\n")
    t = time.time()
    while time.time() - t < 3:
        line = s.readline().decode("utf-8", "replace").strip()
        if line.startswith("STATUS"):
            return dict(p.split("=", 1) for p in line.split()[1:] if "=" in p)
    return {}


def shot(s):
    """Liefert ein PIL-Bild oder None."""
    s.reset_input_buffer()
    s.write(b"SHOT\n")
    t = time.time()
    w = h = 0
    while time.time() - t < 5:
        line = s.readline().decode("utf-8", "replace").strip()
        if line.startswith("SHOT "):
            _, w, h = line.split()
            w, h = int(w), int(h)
            break
    if not w:
        return None
    need = w * h * 2
    data = bytearray()
    s.timeout = 10
    while len(data) < need:
        chunk = s.read(need - len(data))
        if not chunk:
            break
        data += chunk
    s.timeout = 2
    if len(data) < need:
        print(f"unvollstaendig: {len(data)} von {need} Bytes", file=sys.stderr)
        return None
    return decode565(bytes(data), w, h)


_LUT = None


def decode565(data, w, h):
    """LovyanGFX liefert swap565: pro Pixel erst das hohe, dann das tiefe Byte von RGB565."""
    global _LUT
    if _LUT is None:
        _LUT = [bytes(((v >> 11) << 3 | (v >> 13), ((v >> 5) & 0x3F) << 2 | ((v >> 9) & 3),
                       (v & 0x1F) << 3 | ((v >> 2) & 7))) for v in range(65536)]
    import struct
    vals = struct.unpack(f">{w * h}H", data)
    return Image.frombytes("RGB", (w, h), b"".join(_LUT[v] for v in vals))


# Beispielkonto in der Kopfzeile (nie ein echter Name auf Bildern fuer die Doku)
NAME = os.environ.get("TALLY32_SHOT_NAME", "jasmin.muster@mail.ch")
# Datum in der Kopfzeile: absichtlich unmoeglich, kennzeichnet die Bilder als Demo-Bilder
DATE = os.environ.get("TALLY32_SHOT_DATE", "30.02.2026")


def demo(s, outdir):
    os.makedirs(outdir, exist_ok=True)
    s.write(f"DATE {DATE}\n".encode())
    s.write(f"AC {NAME}\n".encode())           # Kopfzeile nicht leer, solange kein Name gesetzt ist
    s.write(b"SET demo 1\n")
    time.sleep(0.5)
    seen, last, n, t0 = set(), None, 0, time.time()
    booking_shots = 0
    while time.time() - t0 < 150 and len(seen) < 9:   # 9 Demo-Schritte
        st = status(s)
        mode, booking = int(st.get("mode", 0)), st.get("booking") == "1"
        if booking:
            time.sleep(1.2)
            img = shot(s)
            if img:
                booking_shots += 1
                n += 1
                img.save(os.path.join(outdir, f"{n:02d}-buchung-{booking_shots}.png"))
                print("gespeichert: buchung", booking_shots)
            continue
        key = (mode, st.get("frei_ab"), st.get("frei_bis"), st.get("quelle"))
        if key != last and mode in NAMES:
            last = key
            time.sleep(1.0)                       # fertig zeichnen lassen
            img = shot(s)
            if img:
                n += 1
                name = NAMES[mode] + ("-" + st.get("quelle", "").lower().replace("_", "-") if mode == 4 else "")
                img.save(os.path.join(outdir, f"{n:02d}-{name}.png"))
                seen.add(key)
                print("gespeichert:", name)
        time.sleep(0.4)
    s.write(b"SET demo 0\n")
    print(f"{n} Bilder in {outdir}")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    port = sys.argv[1]
    s = open_port(port)
    try:
        if sys.argv[2] == "--demo":
            demo(s, sys.argv[3] if len(sys.argv) > 3 else "screens")
        else:
            s.write(f"DATE {DATE}\n".encode())
            s.write(f"AC {NAME}\n".encode())   # Kopfzeile nicht leer, solange kein Name gesetzt ist
            time.sleep(1.5)
            img = shot(s)
            if not img:
                sys.exit("kein Bild erhalten")
            img.save(sys.argv[2])
            print("gespeichert:", sys.argv[2])
    finally:
        s.close()


if __name__ == "__main__":
    main()
