#!/usr/bin/env python3
"""
flash.py - Firmware nur auf das richtige Display flashen

  agent/.venv/bin/python tools/flash.py 7      7"-Variante  (env guition-p4)
  agent/.venv/bin/python tools/flash.py 10     10"-Variante (env guition-p4-10)

Liest unmittelbar vor dem Flashen die USB-Seriennummer des angeschlossenen Displays und
schlaegt sie in ~/.config/tally32/displays.json nach, z. B.

  {"12:34:56:78:9A:BC": "7", "AB:CD:EF:01:23:45": "10"}

Passt das Display nicht zur gewuenschten Variante, ist es unbekannt oder haengen mehrere
Displays am Rechner, wird nicht geflasht. Die falsche Variante startet auf dem anderen
Panel nicht und das Display ist nur noch ueber den BOOT-Download-Modus erreichbar.
"""
import json
import os
import subprocess
import sys
import time

import serial
import serial.tools.list_ports

ENVS = {"7": "guition-p4", "10": "guition-p4-10"}
MAP = os.path.expanduser("~/.config/tally32/displays.json")
PIO = os.path.expanduser("~/.pio-venv/bin/pio")
FW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware")
ESPRESSIF_VID = 0x303A


def displays():
    return [p for p in serial.tools.list_ports.comports() if p.vid == ESPRESSIF_VID]


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ENVS:
        sys.exit(__doc__)
    want = sys.argv[1]
    try:
        known = {k.upper(): v for k, v in json.load(open(MAP)).items()}
    except (OSError, ValueError) as e:
        sys.exit(f"Zuordnung {MAP} fehlt oder ist ungueltig ({e}).")

    found = displays()
    if len(found) != 1:
        sys.exit(f"Genau ein Display anschliessen (gefunden: {len(found)}). Nicht geflasht.")
    port = found[0]
    sn = (port.serial_number or "").upper()
    have = known.get(sn)
    if have is None:
        sys.exit(f"Unbekanntes Display {sn} an {port.device}. In {MAP} eintragen. Nicht geflasht.")
    if have != want:
        sys.exit(f"An {port.device} haengt das {have}\"-Display ({sn}), gewuenscht ist {want}\". Nicht geflasht.")

    print(f"{want}\"-Display {sn} an {port.device}, flashe {ENVS[want]} ...", flush=True)
    r = subprocess.run([PIO, "run", "-d", FW, "-e", ENVS[want], "-t", "upload", "--upload-port", port.device])
    if r.returncode:
        sys.exit("Flashen fehlgeschlagen.")

    # Danach muss dasselbe Display mit der Firmware antworten
    deadline = time.time() + 30
    while time.time() < deadline:
        now = [p for p in displays() if (p.serial_number or "").upper() == sn]
        if now:
            try:
                s = serial.Serial()
                s.port, s.baudrate, s.timeout, s.dtr, s.rts = now[0].device, 115200, 1, False, False
                s.open()
                time.sleep(4)                     # Oeffnen startet das Display neu
                s.reset_input_buffer()
                s.write(b"HELLO\n")
                t = time.time()
                while time.time() - t < 3:
                    if s.readline().startswith(b"TALLY32"):
                        print("OK: Display laeuft mit der neuen Firmware.")
                        return
                s.close()
            except Exception:  # noqa: BLE001 - Display meldet sich beim Neustart kurz ab
                pass
        time.sleep(2)
    sys.exit("Display antwortet nach dem Flashen nicht. Am Geraet pruefen.")


if __name__ == "__main__":
    main()
