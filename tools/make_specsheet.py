#!/usr/bin/env python3
"""
make_specsheet.py - Spec Sheet "Tally32 Agent" fuer die ICT als PDF (Deutsch oder Englisch)

  python3 tools/make_specsheet.py [--lang de|en] [ausgabe.pdf]

Standard-Ausgabe: docs/Tally32_Agent_SpecSheet_ICT.pdf (de)
                  docs/Tally32_Agent_SpecSheet_IT_EN.pdf (en)
"""
import argparse
import datetime as dt
import os
import tempfile

from PIL import Image as PILImage, ImageDraw

from reportlab.graphics.shapes import Drawing, Line, Polygon, Rect, String
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (Image, KeepTogether, PageBreak, Paragraph, SimpleDocTemplate, Spacer,
                                Table, TableStyle)

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT_DEFAULT = {"de": os.path.join(ROOT, "docs", "Tally32_Agent_SpecSheet_ICT.pdf"),
               "en": os.path.join(ROOT, "docs", "Tally32_Agent_SpecSheet_IT_EN.pdf")}
VERSION = "1.1"
DATE = dt.date.today().strftime("%m/%Y")             # wie im Flyer, z. B. 10/2026
LANG = "de"                                          # wird in main() gesetzt


def L(de, en):
    """Text in der gewaehlten Sprache."""
    return en if LANG == "en" else de


pdfmetrics.registerFont(TTFont("Montserrat", os.path.join(ROOT, "tools/fonts/Montserrat-Medium.ttf")))

# leeres Ankreuzkaestchen als kleines Bild (die eingebauten PDF-Schriften haben kein Kaestchen)
BOX_PNG = os.path.join(tempfile.gettempdir(), "tally32_checkbox.png")
_im = PILImage.new("RGB", (40, 40), "white")
ImageDraw.Draw(_im).rectangle([2, 2, 37, 37], outline=(67, 32, 120), width=3)
_im.save(BOX_PNG)
CB = f"<img src='{BOX_PNG}' width='8' height='8' valign='-1'/>"

PURPLE = colors.HexColor("#432078")
LILAC = colors.HexColor("#B69AF4")
LIGHT = colors.HexColor("#F1ECFB")
GREY = colors.HexColor("#5F5F66")
LINE = colors.HexColor("#D9D6CC")

st_h1 = ParagraphStyle("h1", fontName="Montserrat", fontSize=20, leading=24, textColor=PURPLE, spaceAfter=4)
st_h2 = ParagraphStyle("h2", fontName="Montserrat", fontSize=12, leading=15, textColor=PURPLE,
                       spaceBefore=7, spaceAfter=4, keepWithNext=1)
st_body = ParagraphStyle("body", fontName="Helvetica", fontSize=8.8, leading=11.9, alignment=TA_LEFT)
st_small = ParagraphStyle("small", parent=st_body, fontSize=8, leading=10.5, textColor=GREY)
st_cell = ParagraphStyle("cell", parent=st_body, fontSize=8.0, leading=10.1)
st_cellb = ParagraphStyle("cellb", parent=st_cell, fontName="Helvetica-Bold")
st_head = ParagraphStyle("head", parent=st_cell, fontName="Helvetica-Bold", textColor=colors.white)
st_bullet = ParagraphStyle("bullet", parent=st_body, leftIndent=10, bulletIndent=0, spaceAfter=1.5)


def P(t, s=st_body):
    return Paragraph(t, s)


def bullets(items):
    return [Paragraph(t, st_bullet, bulletText="•") for t in items]


def table(rows, widths, head=True, zebra=True):
    data = [[P(c, st_head if (head and i == 0) else (st_cellb if j == 0 else st_cell)) for j, c in enumerate(r)]
            for i, r in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if head else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"),
             ("LINEBELOW", (0, 0), (-1, -1), 0.4, LINE),
             ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if head:
        style.append(("BACKGROUND", (0, 0), (-1, 0), PURPLE))
    if zebra:
        for i in range(1 if head else 0, len(rows)):
            if i % 2 == 0:
                style.append(("BACKGROUND", (0, i), (-1, i), LIGHT))
    t.setStyle(TableStyle(style))
    return t


def box(text, bg=LIGHT, border=LILAC):
    t = Table([[P(text, st_body)]], colWidths=[174 * mm])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), bg), ("BOX", (0, 0), (-1, -1), 0.8, border),
                           ("LEFTPADDING", (0, 0), (-1, -1), 7), ("RIGHTPADDING", (0, 0), (-1, -1), 7),
                           ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    return t


def diagram():
    """Datenfluss: Microsoft 365 <-> Agent (Benutzerkonto) <-> Display."""
    w, h = 174 * mm, 38 * mm
    d = Drawing(w, h)
    boxes = [(0, "Microsoft 365", [L("Outlook-Kalender", "Outlook calendar"), L("Teams-Status", "Teams status")]),
             (65 * mm, "Tally32 Agent", [L("Windows, Benutzerkonto", "Windows, user account"),
                                         L("ohne Adminrechte", "no admin rights")]),
             (130 * mm, "Tally32 Display", [L("ESP32, USB-Seriell", "ESP32, USB serial"),
                                            L("am Arbeitsplatz", "at the desk")])]
    bw, bh, by = 44 * mm, 24 * mm, 9 * mm
    for x, title, lines in boxes:
        d.add(Rect(x, by, bw, bh, rx=3 * mm, ry=3 * mm, fillColor=LIGHT, strokeColor=PURPLE, strokeWidth=1))
        d.add(String(x + bw / 2, by + bh - 7 * mm, title, fontName="Montserrat", fontSize=10,
                     fillColor=PURPLE, textAnchor="middle"))
        for k, ln in enumerate(lines):
            d.add(String(x + bw / 2, by + bh - 13 * mm - k * 4.2 * mm, ln, fontName="Helvetica", fontSize=7.8,
                         fillColor=colors.black, textAnchor="middle"))

    def arrow(x1, x2, y, label, sub):
        d.add(Line(x1, y, x2, y, strokeColor=GREY, strokeWidth=1))
        for xa, dirn in ((x1, 1), (x2, -1)):
            d.add(Polygon([xa, y, xa + dirn * 2.2 * mm, y + 1.2 * mm, xa + dirn * 2.2 * mm, y - 1.2 * mm],
                          fillColor=GREY, strokeColor=GREY))
        d.add(String((x1 + x2) / 2, y + 2.2 * mm, label, fontName="Helvetica-Bold", fontSize=7.4,
                     fillColor=colors.black, textAnchor="middle"))
        d.add(String((x1 + x2) / 2, y - 4.4 * mm, sub, fontName="Helvetica", fontSize=7, fillColor=GREY,
                     textAnchor="middle"))
    arrow(45 * mm, 64 * mm, by + bh / 2, "HTTPS 443", L("ausgehend", "outbound"))
    arrow(110 * mm, 129 * mm, by + bh / 2, L("USB-Kabel", "USB cable"), L("nur seriell", "serial only"))
    d.add(String(w / 2, 3 * mm, L("Keine eingehenden Verbindungen, kein offener Port, keine Daten an Dritte.",
                                  "No inbound connections, no open port, no data sent to third parties."),
                 fontName="Helvetica-Oblique", fontSize=7.6, fillColor=GREY, textAnchor="middle"))
    return d


def on_page(c, doc):
    c.saveState()
    c.setFillColor(PURPLE)
    c.rect(0, A4[1] - 9 * mm, A4[0], 9 * mm, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont("Montserrat", 9)
    c.drawString(18 * mm, A4[1] - 6 * mm, L("Tally32 Agent  ·  Spec Sheet für die ICT",
                                            "Tally32 Agent  ·  Spec sheet for IT"))
    c.drawRightString(A4[0] - 18 * mm, A4[1] - 6 * mm, f"Version {VERSION}  ·  {DATE}")
    c.setFillColor(GREY)
    c.setFont("Helvetica", 7.5)
    c.drawString(18 * mm, 10 * mm, L("Grundlage: Quellcode agent/tally_agent.py, github.com/AussieCH/Tally32",
                                     "Based on: source code agent/tally_agent.py, github.com/AussieCH/Tally32"))
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, L(f"Seite {doc.page}", f"Page {doc.page}"))
    c.restoreState()


def build():
    s = []
    logo = os.path.join(ROOT, "Logos/Tally32_logo.png")
    s.append(Image(logo, width=54 * mm, height=54 * mm * 280 / 1060))
    s.append(Spacer(1, 2 * mm))
    s.append(P(L("Spec Sheet: lokaler Client (Tally32 Agent)", "Spec sheet: local client (Tally32 Agent)"), st_h1))
    s.append(P(L("Funktionsumfang, Berechtigungen und Risikobeurteilung",
                 "Features, permissions and risk assessment"), ParagraphStyle(
        "sub", parent=st_body, fontName="Montserrat", fontSize=11, textColor=GREY, spaceAfter=6)))
    s.append(table([
        [L("Ansprechperson", "Contact"), "Oliver Timme"],
        [L("Abteilung / Arbeitsplatz", "Department / workstation"), "&nbsp;"],
        [L("Betriebsart", "Mode of operation"),
         L("Standalone: ein Display pro Arbeitsplatz, der Agent läuft auf dem eigenen "
           "Windows-Rechner unter dem eigenen Benutzerkonto",
           "Standalone: one display per workstation. The agent runs on the user's own "
           "Windows PC under the user's own account")],
        [L("Funktionsumfang", "Features"),
         L("Statusanzeige <b>und Terminbuchung am Display</b> (beides Kernfunktionen), "
           "dafür das Recht Calendars.ReadWrite",
           "Status display <b>and appointment booking on the display</b> (both core features), "
           "which needs the Calendars.ReadWrite permission")],
        [L("Stand der Angaben", "Information as of"),
         L(f"{DATE}, Agent und Firmware Version {VERSION}, gemäss Quellcode. Auf Windows 11 "
           "erprobt.",
           f"{DATE}, agent and firmware version {VERSION}, based on the source code. Tested on "
           "Windows 11.")],
    ], [42 * mm, 132 * mm], head=False))

    s.append(P(L("1. Zweck", "1. Purpose"), st_h2))
    s.append(P(L("Tally32 ist eine kleine Statusanzeige am Arbeitsplatz (Display mit 7 oder 10,1 Zoll, per USB "
                 "angeschlossen). "
                 "Sie zeigt Kolleginnen, Kollegen und Besuchern, ob man gerade <b>telefoniert</b> (ON AIR), "
                 "<b>besetzt</b> ist und <b>ab wann man wieder frei</b> ist, oder ob man nicht am Platz ist. "
                 "Zweite Kernfunktion ist die <b>Terminbuchung</b>: Wer den Benutzer nicht am Platz antrifft, "
                 "reserviert direkt am Display einen freien Termin und erhält eine Einladung. "
                 "Der Agent ist das Programm auf dem Rechner, das diese Informationen ermittelt und per "
                 "USB-Kabel an das Display schickt.",
                 "Tally32 is a small status display at the workstation (7 or 10.1 inch screen, connected "
                 "via USB). "
                 "It shows colleagues and visitors whether the user is <b>on a call</b> (ON AIR), "
                 "<b>busy</b> and <b>free again from when</b>, or away from the desk. "
                 "The second core feature is <b>appointment booking</b>: if the user is not at the desk, a "
                 "visitor can book a free slot directly on the display and receives an invitation. "
                 "The agent is the program on the PC that collects this information and sends it to the "
                 "display over the USB cable.")))
    s.append(Spacer(1, 3 * mm))
    s.append(diagram())

    s.append(P(L("2. Was der Agent macht", "2. What the agent does"), st_h2))
    s.append(table([
        [L("Funktion", "Function"), L("Quelle", "Source"), L("Daten, die gelesen werden", "Data read"),
         L("Intervall", "Interval")],
        [L("Anruf erkennen", "Call detection"),
         L("Windows-Registry (HKCU), nur lesen: Mikrofon-Nutzungsanzeige von Windows "
           "(CapabilityAccessManager, ConsentStore microphone)",
           "Windows registry (HKCU), read only: Windows microphone usage indicator "
           "(CapabilityAccessManager, ConsentStore microphone)"),
         L("nur ob eine App gerade das Mikrofon benutzt und "
           "welche (z.&nbsp;B. Teams, 3CX). <b>Keine Audiodaten.</b>",
           "only whether an app is currently using the microphone and "
           "which one (e.g. Teams, 3CX). <b>No audio data.</b>"), "2 s"],
        [L("Bildschirmsperre", "Screen lock"), L("Windows-API WTSQuerySessionInformation",
                                                 "Windows API WTSQuerySessionInformation"),
         L("nur gesperrt ja oder nein", "only locked yes or no"), "2 s"],
        [L("Teams-Status", "Teams status"), "Microsoft Graph GET /me/presence",
         L("Verfügbarkeit und Aktivität (z.&nbsp;B. Busy, InACall)",
           "availability and activity (e.g. Busy, InACall)"), "10 s"],
        [L("Kalender", "Calendar"), L("Microsoft Graph GET /me/calendarView, heute und morgen",
                                      "Microsoft Graph GET /me/calendarView, today and tomorrow"),
         L("nur <b>Beginn, Ende, Frei/Gebucht-Status, abgesagt</b> (Feldauswahl $select=showAs,start,end,"
           "isCancelled,isAllDay). Kein Betreff, kein Ort, keine Teilnehmer, kein Inhalt.",
           "only <b>start, end, free/busy status, cancelled</b> (field selection $select=showAs,start,end,"
           "isCancelled,isAllDay). No subject, no location, no attendees, no body."), "60 s"],
        [L("Terminbuchung (Kernfunktion)", "Appointment booking (core feature)"),
         L("Microsoft Graph GET /me/calendarView (2 Wochen) und POST /me/events",
           "Microsoft Graph GET /me/calendarView (2 weeks) and POST /me/events"),
         L("liest dieselben Zeitfelder. Legt auf Anfrage vom Display einen Termin im eigenen Kalender an, mit "
           "Titel und E-Mail-Adresse, die der Besucher eingegeben hat. Exchange verschickt dabei die Einladung "
           "an den Besucher.",
           "reads the same time fields. On request from the display, creates an appointment in the user's own "
           "calendar with the title and email address entered by the visitor. Exchange then sends the "
           "invitation to the visitor."), L("nur bei Buchung", "only when booking")],
        [L("Ausgabe ans Display", "Output to display"),
         L("USB-Seriell (COM-Port, 115200 Baud)", "USB serial (COM port, 115200 baud)"),
         L("Status, Uhrzeit, «frei ab» bzw. «frei bis», "
           "Terminblöcke von heute und morgen (nur Zeiten), Name des Kontos",
           "status, time, \"free from\" or \"free until\", "
           "appointment blocks for today and tomorrow (times only), account name"),
         L("2 bis 30 s", "2 to 30 s")],
    ], [27 * mm, 47 * mm, 82 * mm, 18 * mm]))

    s.append(KeepTogether([P(L("3. Was der Agent nicht macht", "3. What the agent does not do"), st_h2)] + bullets([
        L("<b>Keine Audio- oder Videoaufnahme.</b> Er öffnet weder Mikrofon noch Kamera, er liest nur die "
          "Windows-Anzeige, ob sie benutzt werden.",
          "<b>No audio or video recording.</b> It opens neither microphone nor camera. It only reads the "
          "Windows indicator showing whether they are in use."),
        L("<b>Liest keine E-Mails, Kontakte, Dateien, Chats</b> und keine Betreffs, Orte, Teilnehmer oder "
          "Inhalte von Terminen.",
          "<b>Reads no emails, contacts, files or chats</b> and no subjects, locations, attendees or "
          "bodies of appointments."),
        L("<b>Nur das eigene Postfach</b> (/me). Kein Zugriff auf Kalender oder Status anderer Personen.",
          "<b>Own mailbox only</b> (/me). No access to other people's calendars or status."),
        L("<b>Keine eingehenden Verbindungen</b>, kein offener Port, kein Webserver auf dem Rechner.",
          "<b>No inbound connections</b>, no open port, no web server on the PC."),
        L("<b>Ausgehend nur</b> login.microsoftonline.com und graph.microsoft.com (HTTPS 443). Keine Telemetrie, "
          "keine Daten an Dritte oder an den Entwickler.",
          "<b>Outbound only</b> to login.microsoftonline.com and graph.microsoft.com (HTTPS 443). No telemetry, "
          "no data sent to third parties or to the developer."),
        L("<b>Keine Administratorrechte</b>, kein Windows-Dienst, keine eigenen Treiber, keine Änderungen an "
          "System- oder Sicherheitseinstellungen.",
          "<b>No administrator rights</b>, no Windows service, no custom drivers, no changes to "
          "system or security settings."),
        L("<b>Keine Tastatur- oder Bildschirmaufzeichnung</b>, keine Fernsteuerung.",
          "<b>No keyboard or screen recording</b>, no remote control."),
        L("<b>Führt nichts aus, was vom Display kommt.</b> Er versteht nur vier feste Textmeldungen: "
          "Anmeldung starten, freie Zeiten abfragen, Termin buchen, Protokollzeile. Alles andere wird ignoriert.",
          "<b>Executes nothing that comes from the display.</b> It understands only four fixed text messages: "
          "start sign-in, query free slots, book appointment, log line. Everything else is ignored."),
        L("<b>Das Display meldet sich nur als COM-Port.</b> Windows sieht einen COM-Port und die "
          "JTAG-Debugschnittstelle des Display-Chips, keine Tastatur (HID) und keinen Massenspeicher. "
          "Der Chip des Displays (ESP32-P4) könnte an diesem Anschluss grundsätzlich auch als anderes "
          "USB-Gerät auftreten (USB-OTG). Die Tally32-Firmware schaltet das nicht ein.",
          "<b>The display only shows up as a COM port.</b> Windows sees a COM port and the "
          "JTAG debug interface of the display chip, no keyboard (HID) and no mass storage. "
          "In principle the display chip (ESP32-P4) could also act as a different USB device on this "
          "port (USB OTG). The Tally32 firmware does not enable this."),
    ])))

    s.append(P(L("4. Technische Angaben", "4. Technical details"), st_h2))
    s.append(table([
        [L("Merkmal", "Item"), L("Angabe", "Details")],
        [L("Programm", "Program"),
         L("Python-Skript agent/tally_agent.py (Quellcode einsehbar), Python 3.11 oder neuer. "
           "Alternativ als einzelne .exe verpackbar (PyInstaller), falls Python nicht erlaubt ist.",
           "Python script agent/tally_agent.py (source code available), Python 3.11 or newer. "
           "Can also be packaged as a single .exe (PyInstaller) if Python is not allowed.")],
        [L("Abhängigkeiten", "Dependencies"),
         L("msal und msal-extensions (Microsoft Authentication Library, von Microsoft) und "
           "pyserial, aus PyPI. msal bringt requests, cryptography und PyJWT mit. Alle Versionen sind in "
           "agent/requirements.txt fest vorgegeben (z.&nbsp;B. msal==1.39.0).",
           "msal and msal-extensions (Microsoft Authentication Library, by Microsoft) and "
           "pyserial, from PyPI. msal pulls in requests, cryptography and PyJWT. All versions are "
           "pinned in agent/requirements.txt (e.g. msal==1.39.0).")],
        [L("Ausführung", "Execution"),
         L("im Benutzerkontext, unsichtbar (pythonw.exe), gestartet über die Aufgabenplanung "
           "«bei Anmeldung», Aufgabe «Tally32 Agent». Eingerichtet mit install_windows.ps1 "
           "ohne erhöhte Rechte.",
           "in the user context, hidden (pythonw.exe), started by Task Scheduler "
           "\"at log on\", task \"Tally32 Agent\". Set up with install_windows.ps1 "
           "without elevated rights.")],
        [L("Dateien", "Files"),
         L("Programm und Python-Umgebung im Projektordner. Einstellungen in "
           "%USERPROFILE%\\.config\\tally32\\config.json. Anmeldetoken mit <b>Windows DPAPI "
           "verschlüsselt</b> in token_cache.bin daneben, nur unter dem eigenen Benutzerkonto lesbar "
           "(macOS: Schlüsselbund). Ein Token aus einer älteren Version wird beim Start übernommen und "
           "die unverschlüsselte Datei gelöscht. Log in "
           "%LOCALAPPDATA%\\tally32\\tally32.log (Statusänderungen und Buchungen mit Datum, Zeit, "
           "E-Mail-Adresse und Titel des Besuchers).",
           "Program and Python environment in the project folder. Settings in "
           "%USERPROFILE%\\.config\\tally32\\config.json. Sign-in token <b>encrypted with Windows "
           "DPAPI</b> in token_cache.bin next to it, readable only under the user's own account "
           "(macOS: Keychain). A token from an older version is migrated at startup and "
           "the unencrypted file is deleted. Log in "
           "%LOCALAPPDATA%\\tally32\\tally32.log (status changes and bookings with date, time, "
           "email address and title entered by the visitor).")],
        [L("USB-Gerät", "USB device"),
         L("GUITION JC1060P470C 7\" oder GUITION JC8012P4A1C 10,1\" (beide ESP32-P4), "
           "eingebaute USB-Schnittstelle des Chips "
           "(USB-Serial/JTAG, USB-VID 303A, PID 1001): COM-Port und JTAG-Debugschnittstelle des "
           "Displays. Treiber in Windows enthalten.",
           "GUITION JC1060P470C 7\" or GUITION JC8012P4A1C 10.1\" (both ESP32-P4), "
           "built-in USB interface of the chip "
           "(USB Serial/JTAG, USB VID 303A, PID 1001): COM port and JTAG debug interface of the "
           "display. Driver included in Windows.")],
        [L("Netzwerk", "Network"),
         L("ausgehend HTTPS 443 zu login.microsoftonline.com und graph.microsoft.com. "
           "Bei der Einrichtung einmalig pypi.org für die Pakete (entfällt mit der .exe).",
           "outbound HTTPS 443 to login.microsoftonline.com and graph.microsoft.com. "
           "During setup, pypi.org once for the packages (not needed with the .exe).")],
        [L("Last", "Load"), L("sehr gering: rund 7 kleine HTTPS-Abfragen pro Minute, wenige MB RAM.",
                              "very low: about 7 small HTTPS requests per minute, a few MB of RAM.")],
    ], [30 * mm, 144 * mm]))

    s.append(P(L("5. Berechtigungen in Microsoft 365", "5. Permissions in Microsoft 365"), st_h2))
    s.append(table([
        [L("Recht (delegiert)", "Permission (delegated)"), L("Wozu", "Purpose"), L("Nötig", "Required")],
        ["Calendars.ReadBasic",
         L("Frei/Gebucht-Zeiten des eigenen Kalenders. Kleinstes Leserecht für "
           "calendarView laut Microsoft, gibt keinen Termintext und keine Anhänge frei. Der Agent fordert davon "
           "nur die Zeitfelder an. Keine Admin-Zustimmung nötig.",
           "Free/busy times of the user's own calendar. Least privileged read permission for "
           "calendarView according to Microsoft. It exposes no appointment text and no attachments. The agent "
           "only requests the time fields. No admin consent required."), L("ja", "yes")],
        ["Presence.Read", L("eigener Teams-Status", "own Teams status"),
         L("optional (abschaltbar)", "optional (can be disabled)")],
        ["Calendars.ReadWrite",
         L("Terminbuchung am Display: Termin im eigenen Kalender anlegen, mit "
           "Einladung an den Besucher. Kernfunktion, im Agenten eingeschaltet (\"booking\": true).",
           "Appointment booking on the display: create an appointment in the user's own calendar with an "
           "invitation to the visitor. Core feature, enabled in the agent (\"booking\": true)."), L("ja", "yes")],
    ], [38 * mm, 106 * mm, 30 * mm]))
    s.append(Spacer(1, 2 * mm))
    s.append(P(L("Anmeldung per Device Code mit dem eigenen Konto, delegiert, also nie mehr Rechte als der "
                 "Benutzer selbst hat. Standardmässig über den öffentlichen Microsoft-Client "
                 "«Microsoft Graph Command Line Tools» (App-ID 14d82eec-204b-4c2f-b7e8-296a70dab67e). Auf "
                 "Wunsch der ICT stattdessen über eine eigene App-Registrierung des Tenants (nur die App-ID "
                 "wird geändert). Dafür braucht es einmal die Zustimmung eines Admins (Abschnitt 10).",
                 "Sign-in uses the device code flow with the user's own account, delegated, so never more rights "
                 "than the user has. By default it uses the public Microsoft client "
                 "\"Microsoft Graph Command Line Tools\" (app ID 14d82eec-204b-4c2f-b7e8-296a70dab67e). If IT "
                 "prefers, a dedicated app registration in the tenant can be used instead (only the app ID "
                 "changes). This needs admin consent once (section 10)."), st_body))

    s.append(P(L("6. Das Display", "6. The display"), st_h2))
    s.extend(bullets([
        L("speichert <b>kein Microsoft-Token</b> und keine Kalenderinhalte, nur die Terminblöcke von heute und "
          "morgen (Zeiten ohne Text, höchstens 16 Stunden gültig), damit es auch abgedockt weiter anzeigt.",
          "stores <b>no Microsoft token</b> and no calendar content, only the appointment blocks for today and "
          "tomorrow (times without text, valid for at most 16 hours), so it keeps working when undocked."),
        L("spannt für die Einrichtung ein <b>eigenes WLAN</b> auf (Tally32-XXXX, WPA2 mit Passwort) mit "
          "passwortgeschütztem Webinterface. Dieses WLAN hat keine Verbindung ins Firmennetz und kein "
          "Internet. Gespeichert sind dort nur Einstellungen, das WLAN-Passwort und der Hash des Web-Passworts. "
          "Das Einrichtungs-WLAN lässt sich im Webinterface oder im Menü am Display <b>abschalten</b>. Wieder einschalten geht nur am "
          "Gerät selbst (Einstellungsmenü mit PIN oder 5 Sekunden aufs Display drücken).",
          "opens its <b>own Wi-Fi</b> for setup (Tally32-XXXX, WPA2 with password) with a "
          "password-protected web interface. This Wi-Fi has no connection to the company network and no "
          "internet. It stores only settings, the Wi-Fi password and the hash of the web password. "
          "The setup Wi-Fi can be <b>turned off</b> in the web interface or in the menu on the display. It can "
          "only be turned back on at the device itself (settings menu with PIN or pressing the display for "
          "5 seconds)."),
        L("zeigt am Platz nur Status und Zeiten, <b>nie Betreffs</b> von Terminen.",
          "shows only status and times at the desk, <b>never subjects</b> of appointments."),
    ]))

    s.append(P(L("7. Risikobeurteilung", "7. Risk assessment"), st_h2))
    s.append(table([
        [L("Risiko", "Risk"), L("Eintritt", "Likelihood"), L("Auswirkung", "Impact"),
         L("Massnahmen", "Controls"), L("Rest", "Residual")],
        [L("Schadsoftware unter dem Benutzerkonto liest das Anmeldetoken",
           "Malware running under the user account reads the sign-in token"), L("gering", "low"),
         L("mittel: eigener Kalender lesbar, mit Buchung auch beschreibbar, bis zum Widerruf",
           "medium: own calendar readable, with booking also writable, until revoked"),
         L("Token mit Windows DPAPI verschlüsselt (nur unter dem eigenen Konto lesbar), nur delegierte Rechte, "
           "jederzeit in Entra widerrufbar. Schreibrecht nur auf den eigenen Kalender, jede Buchung im Log "
           "des Agenten nachvollziehbar.",
           "Token encrypted with Windows DPAPI (readable only under the user's own account), delegated rights "
           "only, can be revoked in Entra at any time. Write access to the user's own calendar only, every "
           "booking traceable in the agent log."), L("gering", "low")],
        [L("Missbrauch der Buchung am Display (unerwünschte Termine oder Einladungen)",
           "Misuse of booking on the display (unwanted appointments or invitations)"),
         L("mittel", "medium"), L("gering", "low"),
         L("nur freie Zeiten im eingestellten Zeitfenster, höchstens 4 Buchungen pro Stunde, Einladung "
           "nur an die eingegebene Adresse. Zeitfenster, Wochentage und Mittagspause im Setup. Jede Buchung "
           "steht im Kalender und im Log, die Funktion lässt sich bei Bedarf am Display abschalten.",
           "only free slots within the configured time window, at most 4 bookings per hour, invitation "
           "only to the address entered. Time window, weekdays and lunch break set in setup. Every booking "
           "appears in the calendar and in the log. The feature can be turned off on the display if needed."),
         L("gering", "low")],
        [L("Zusätzliches WLAN im Büro durch das Display", "Additional Wi-Fi in the office from the display"),
         L("sicher", "certain"), L("gering", "low"),
         L("WPA2 mit eigenem Passwort, kein Routing ins Firmennetz, Webinterface mit Passwort und "
           "Pflicht-Passwortwechsel. Einrichtungs-WLAN im Webinterface abschaltbar, wieder einschalten nur "
           "am Gerät.",
           "WPA2 with its own password, no routing into the company network, web interface with password and "
           "mandatory password change. Setup Wi-Fi can be turned off in the web interface and turned back on "
           "only at the device."), L("gering", "low")],
        [L("Manipuliertes Display oder USB-Gerät", "Tampered display or USB device"),
         L("gering", "low"), L("gering", "low"),
         L("Agent nimmt nur vier feste Textmeldungen an, keine Codeausführung. Das Display meldet sich nur "
           "als COM-Port und JTAG-Schnittstelle, keine Tastatur. Eine andere Firmware könnte das ändern, "
           "das setzt aber physischen Zugriff auf das Display voraus.",
           "Agent accepts only four fixed text messages, no code execution. The display only shows up "
           "as a COM port and JTAG interface, no keyboard. Different firmware could change this, "
           "but that requires physical access to the display."), L("gering", "low")],
        [L("Abhängigkeiten aus PyPI (Lieferkette)", "Dependencies from PyPI (supply chain)"),
         L("gering", "low"), L("mittel", "medium"),
         L("nur drei verbreitete Pakete (msal und msal-extensions von Microsoft, pyserial). Alle Versionen "
           "inklusive indirekter Pakete festgeschrieben, wahlweise eine geprüfte .exe.",
           "only three widely used packages (msal and msal-extensions by Microsoft, pyserial). All versions "
           "including indirect packages pinned, optionally a reviewed .exe."), L("gering", "low")],
        [L("Anmeldung per Device Code (bekanntes Phishing-Muster)",
           "Sign-in via device code (known phishing pattern)"), L("gering", "low"), L("gering", "low"),
         L("Anmeldung startet nur der Benutzer selbst, das Token landet auf dem eigenen Rechner. Sperrt Conditional "
           "Access den Device Code Flow, ist eine eigene App-Registrierung nötig.",
           "Only the user starts the sign-in, and the token ends up on the user's own PC. If Conditional "
           "Access blocks the device code flow, a dedicated app registration is needed."), L("gering", "low")],
        [L("Datenschutz: Verfügbarkeit sichtbar am Platz", "Privacy: availability visible at the desk"),
         L("beabsichtigt", "intended"), L("gering", "low"),
         L("nur Status und Zeiten, keine Betreffs, entspricht dem Teams-Status. Im Bildschirmschoner ist nur "
           "das Logo zu sehen.",
           "only status and times, no subjects, same as the Teams status. The screensaver shows only "
           "the logo."), L("gering", "low")],
    ], [40 * mm, 21 * mm, 29 * mm, L(70, 67) * mm, L(14, 17) * mm]))   # en: "Residual" braucht Platz
    s.append(Spacer(1, 3 * mm))
    s.append(box(L("<b>Gesamtbeurteilung: geringes Risiko.</b> Der Agent arbeitet mit den Rechten des Benutzers, "
                   "liest nur Zeiten und Status des eigenen Kontos, öffnet keine Ports und spricht nur mit "
                   "Microsoft. Geschrieben wird nur für die Terminbuchung und nur in den eigenen Kalender. "
                   "Das Token liegt verschlüsselt im Benutzerprofil.",
                   "<b>Overall assessment: low risk.</b> The agent works with the user's rights, "
                   "reads only times and status of the user's own account, opens no ports and talks only to "
                   "Microsoft. It writes only for appointment booking and only to the user's own calendar. "
                   "The token is stored encrypted in the user profile.")))

    s.append(P(L("8. Schutzmassnahmen und Hinweise für den Betrieb", "8. Safeguards and operating notes"), st_h2))
    s.extend(bullets([
        L("Anmeldetoken verschlüsselt ablegen (Windows DPAPI). <i>Umgesetzt.</i>",
          "Store the sign-in token encrypted (Windows DPAPI). <i>Implemented.</i>"),
        L("Paketversionen festschreiben (agent/requirements.txt). <i>Umgesetzt.</i>",
          "Pin package versions (agent/requirements.txt). <i>Implemented.</i>"),
        L("Einrichtungs-WLAN des Displays mit eigenem Passwort versehen und nach der Einrichtung abschalten "
          "(Webinterface, Abschnitt Einrichtungs-WLAN). <i>Umgesetzt.</i>",
          "Give the display's setup Wi-Fi its own password and turn it off after setup "
          "(web interface, setup Wi-Fi section). <i>Implemented.</i>"),
        L("Wahlweise eigene App-Registrierung des Tenants statt des öffentlichen Microsoft-Clients.",
          "Optionally use a dedicated app registration in the tenant instead of the public Microsoft client."),
        L("In einer allfälligen USB-Gerätekontrolle nur das Gerät VID 303A / PID 1001 freigeben, und dort "
          "nur die Klassen COM-Port (CDC) und JTAG, keine Tastatur (HID).",
          "If USB device control is in place, allow only the device VID 303A / PID 1001, and for it "
          "only the classes COM port (CDC) and JTAG, no keyboard (HID)."),
    ]))

    s.append(P(L("9. Widerruf und Deinstallation", "9. Revocation and uninstall"), st_h2))
    s.extend(bullets([
        L("Anmeldung löschen: <font face='Courier'>tally_agent.py logout</font> (löscht das gespeicherte Token).",
          "Delete sign-in: <font face='Courier'>tally_agent.py logout</font> (deletes the stored token)."),
        L("Zugriff serverseitig sperren: in Entra die Anmeldesitzungen des Benutzers widerrufen oder die "
          "Zustimmung für die App entfernen.",
          "Block access on the server side: revoke the user's sign-in sessions in Entra or remove the "
          "consent for the app."),
        L("Entfernen: <font face='Courier'>install_windows.ps1 -Uninstall</font> (Aufgabe und Prozess), danach "
          "Projektordner und %USERPROFILE%\\.config\\tally32 löschen.",
          "Remove: <font face='Courier'>install_windows.ps1 -Uninstall</font> (task and process), then delete "
          "the project folder and %USERPROFILE%\\.config\\tally32."),
    ]))

    s.append(PageBreak())
    s.append(P(L("10. Warum einmal eine Admin-Freigabe nötig ist", "10. Why admin consent is needed once"),
               st_h2))
    s.append(P(L("Auf dem Rechner braucht der Agent <b>keine Adminrechte</b>. Einmalig nötig ist aber eine "
                 "<b>Zustimmung (Consent) in Microsoft Entra ID</b>, damit der Agent im Namen des Benutzers auf "
                 "dessen eigenen Kalender und Teams-Status zugreifen darf.",
                 "The agent needs <b>no admin rights</b> on the PC. However, <b>consent in Microsoft Entra "
                 "ID</b> is needed once, so that the agent may access the user's own calendar and Teams status "
                 "on the user's behalf."), st_body))
    s.append(Spacer(1, 2 * mm))
    s.append(table([
        [L("Was", "What"),
         L("Administratorzustimmung für die App «Microsoft Graph Command Line Tools» "
           "(oder eine eigene App-Registrierung) mit den delegierten Rechten Calendars.ReadBasic, "
           "Calendars.ReadWrite und Presence.Read",
           "Admin consent for the app \"Microsoft Graph Command Line Tools\" "
           "(or a dedicated app registration) with the delegated permissions Calendars.ReadBasic, "
           "Calendars.ReadWrite and Presence.Read")],
        [L("Warum", "Why"),
         L("Viele Tenants erlauben Benutzern nicht, Apps selbst zuzustimmen, und Schreibrechte auf den "
           "Kalender gibt meist nur ein Admin frei. Ohne Zustimmung bricht die Anmeldung mit "
           "«Administratorgenehmigung erforderlich» ab.",
           "Many tenants do not let users consent to apps themselves, and write access to the "
           "calendar is usually granted only by an admin. Without consent, sign-in fails with "
           "\"Need admin approval\".")],
        [L("Wie oft", "How often"),
         L("einmalig. Danach meldet sich der Benutzer selbst an, der Agent erneuert seine Tokens ohne "
           "weitere Freigabe.",
           "once. After that the user signs in, and the agent renews its tokens without "
           "further approval.")],
        [L("Was sie nicht bedeutet", "What it does not mean"),
         L("keine Adminrechte auf dem Rechner, kein Zugriff auf andere Postfächer "
           "(delegiert, nur das eigene Konto), keine Anwendungsberechtigung (App-only), "
           "kein Zugriff auf E-Mails, Dateien oder Chats",
           "no admin rights on the PC, no access to other mailboxes "
           "(delegated, own account only), no application permission (app-only), "
           "no access to emails, files or chats")],
        [L("Eingrenzen", "Limit scope"),
         L("Die Zustimmung lässt sich auf diesen einen Benutzer beschränken statt auf die "
           "ganze Organisation (OAuth2-Berechtigung mit consentType Principal, siehe unten). "
           "Alternativ eine eigene App-Registrierung, die nur diesem Benutzer zugewiesen ist.",
           "Consent can be limited to this one user instead of the "
           "whole organization (OAuth2 permission grant with consentType Principal, see below). "
           "Alternatively, a dedicated app registration assigned only to this user.")],
        [L("Rückgängig", "Undo"),
         L("in Entra unter Unternehmensanwendungen die Berechtigung entfernen, gilt sofort",
           "remove the permission in Entra under Enterprise applications, takes effect immediately")],
    ], [36 * mm, 138 * mm], head=False))
    s.append(Spacer(1, 2 * mm))
    s.append(P(L("Zustimmung nur für einen Benutzer (Microsoft Graph PowerShell, Platzhalter ersetzen):",
                 "Consent for one user only (Microsoft Graph PowerShell, replace the placeholders):"), st_small))
    s.append(P("<font face='Courier' size='7.5'>New-MgOauth2PermissionGrant -BodyParameter @{<br/>"
               "&nbsp;&nbsp;clientId&nbsp;&nbsp;&nbsp;&nbsp;= \"&lt;"
               + L("Objekt-ID Service Principal der App", "object ID of the app's service principal")
               + "&gt;\"<br/>"
               "&nbsp;&nbsp;consentType = \"Principal\"<br/>"
               "&nbsp;&nbsp;principalId = \"&lt;" + L("Objekt-ID des Benutzers", "object ID of the user")
               + "&gt;\"<br/>"
               "&nbsp;&nbsp;resourceId&nbsp;&nbsp;= \"&lt;"
               + L("Objekt-ID Service Principal Microsoft Graph", "object ID of the Microsoft Graph service principal")
               + "&gt;\"<br/>"
               "&nbsp;&nbsp;scope&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; = \"Calendars.ReadBasic Calendars.ReadWrite Presence.Read\"<br/>"
               "}</font>", st_body))

    s.append(P(L("11. Auf einen Blick", "11. At a glance"), st_h2))
    s.append(table([
        [L("Punkt", "Item"), L("Angabe", "Details")],
        [L("Programm", "Program"),
         L("Tally32 Agent, Python-Skript, läuft unter dem eigenen Benutzerkonto ohne Adminrechte",
           "Tally32 Agent, Python script, runs under the user's own account without admin rights")],
        [L("Liest", "Reads"),
         L("eigenen Teams-Status, Zeiten und Frei/Gebucht des eigenen Kalenders, ob das Mikrofon "
           "benutzt wird, ob der Bildschirm gesperrt ist",
           "own Teams status, times and free/busy of the own calendar, whether the microphone "
           "is in use, whether the screen is locked")],
        [L("Liest nicht", "Does not read"),
         L("E-Mails, Dateien, Chats, Kontakte, Betreffs, Teilnehmer und Inhalte von Terminen, Audio",
           "emails, files, chats, contacts, subjects, attendees and bodies of appointments, audio")],
        [L("Schreibt", "Writes"),
         L("Terminbuchung (Kernfunktion): Termine im eigenen Kalender mit Einladung an den Besucher",
           "appointment booking (core feature): appointments in the own calendar with invitation to the visitor")],
        [L("Netzwerk", "Network"),
         L("nur ausgehend HTTPS zu login.microsoftonline.com und graph.microsoft.com, kein offener Port",
           "outbound HTTPS only to login.microsoftonline.com and graph.microsoft.com, no open port")],
        [L("Rechte Microsoft 365", "Microsoft 365 permissions"),
         L("delegiert: Calendars.ReadBasic, Calendars.ReadWrite (Terminbuchung), "
           "Presence.Read, einmalige Admin-Zustimmung",
           "delegated: Calendars.ReadBasic, Calendars.ReadWrite (appointment booking), "
           "Presence.Read, one-time admin consent")],
        [L("Gerät", "Device"),
         L("Display per USB (nur COM-Port und JTAG des Displays), eigenes Einrichtungs-WLAN mit WPA2, abschaltbar",
           "display via USB (only COM port and JTAG of the display), own setup Wi-Fi with WPA2, can be "
           "turned off")],
        [L("Gesamtrisiko", "Overall risk"), L("<b>gering</b> (Abschnitt 7)", "<b>low</b> (section 7)")],
        [L("Widerruf", "Revocation"),
         L("jederzeit: Token löschen, Sitzungen in Entra widerrufen, Aufgabe entfernen",
           "at any time: delete token, revoke sessions in Entra, remove scheduled task")],
    ], [38 * mm, 136 * mm]))
    return s


def main():
    global LANG
    ap = argparse.ArgumentParser(description="Tally32 Agent Spec Sheet als PDF")
    ap.add_argument("--lang", choices=("de", "en"), default="de")
    ap.add_argument("out", nargs="?", help="Ausgabe-PDF (Standard je nach Sprache)")
    args = ap.parse_args()
    LANG = args.lang
    out = args.out or OUT_DEFAULT[LANG]

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    doc = SimpleDocTemplate(out, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm,
                            bottomMargin=16 * mm,
                            title=L("Tally32 Agent: Spec Sheet für die ICT", "Tally32 Agent: Spec sheet for IT"),
                            author="Oliver Timme",
                            subject=L("Lokaler Client, Funktionsumfang und Risikobeurteilung",
                                      "Local client, features and risk assessment"))
    doc.build(build(), onFirstPage=on_page, onLaterPages=on_page)
    print("PDF:", out)


if __name__ == "__main__":
    main()
