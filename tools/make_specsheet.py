#!/usr/bin/env python3
"""
make_specsheet.py - Spec Sheet "Tally32 Agent" fuer die ICT als PDF

  python3 tools/make_specsheet.py [ausgabe.pdf]

Standard-Ausgabe: docs/Tally32_Agent_SpecSheet_ICT.pdf
"""
import datetime as dt
import os
import sys
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
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "Tally32_Agent_SpecSheet_ICT.pdf")
VERSION = "1.1"
DATE = dt.date.today().strftime("%m/%Y")             # wie im Flyer, z. B. 10/2026

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
    boxes = [(0, "Microsoft 365", ["Outlook-Kalender", "Teams-Status"]),
             (65 * mm, "Tally32 Agent", ["Windows, Benutzerkonto", "ohne Adminrechte"]),
             (130 * mm, "Tally32 Display", ["ESP32, USB-Seriell", "am Arbeitsplatz"])]
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
    arrow(45 * mm, 64 * mm, by + bh / 2, "HTTPS 443", "ausgehend")
    arrow(110 * mm, 129 * mm, by + bh / 2, "USB-Kabel", "nur seriell")
    d.add(String(w / 2, 3 * mm, "Keine eingehenden Verbindungen, kein offener Port, keine Daten an Dritte.",
                 fontName="Helvetica-Oblique", fontSize=7.6, fillColor=GREY, textAnchor="middle"))
    return d


def on_page(c, doc):
    c.saveState()
    c.setFillColor(PURPLE)
    c.rect(0, A4[1] - 9 * mm, A4[0], 9 * mm, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont("Montserrat", 9)
    c.drawString(18 * mm, A4[1] - 6 * mm, "Tally32 Agent  ·  Spec Sheet für die ICT")
    c.drawRightString(A4[0] - 18 * mm, A4[1] - 6 * mm, f"Version {VERSION}  ·  {DATE}")
    c.setFillColor(GREY)
    c.setFont("Helvetica", 7.5)
    c.drawString(18 * mm, 10 * mm, "Grundlage: Quellcode agent/tally_agent.py, github.com/AussieCH/Tally32")
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, f"Seite {doc.page}")
    c.restoreState()


def build():
    s = []
    logo = os.path.join(ROOT, "Logos/Tally32_logo.png")
    s.append(Image(logo, width=54 * mm, height=54 * mm * 280 / 1060))
    s.append(Spacer(1, 2 * mm))
    s.append(P("Spec Sheet: lokaler Client (Tally32 Agent)", st_h1))
    s.append(P("Funktionsumfang, Berechtigungen und Risikobeurteilung", ParagraphStyle(
        "sub", parent=st_body, fontName="Montserrat", fontSize=11, textColor=GREY, spaceAfter=6)))
    s.append(table([
        ["Ansprechperson", "Oliver Timme"],
        ["Abteilung / Arbeitsplatz", "&nbsp;"],
        ["Betriebsart", "Standalone: ein Display pro Arbeitsplatz, der Agent läuft auf dem eigenen "
                        "Windows-Rechner unter dem eigenen Benutzerkonto"],
        ["Funktionsumfang", "Statusanzeige <b>und Terminbuchung am Display</b> (beides Kernfunktionen), "
                            "dafür das Recht Calendars.ReadWrite"],
        ["Stand der Angaben", f"{DATE}, Agent und Firmware Version {VERSION}, gemäss Quellcode. Auf Windows 11 "
                              "erprobt."],
    ], [42 * mm, 132 * mm], head=False))

    s.append(P("1. Zweck", st_h2))
    s.append(P("Tally32 ist eine kleine Statusanzeige am Arbeitsplatz (Display mit 7 oder 10,1 Zoll, per USB "
               "angeschlossen). "
               "Sie zeigt Kolleginnen, Kollegen und Besuchern, ob man gerade <b>telefoniert</b> (ON AIR), "
               "<b>besetzt</b> ist und <b>ab wann man wieder frei</b> ist, oder ob man nicht am Platz ist. "
               "Zweite Kernfunktion ist die <b>Terminbuchung</b>: Wer den Benutzer nicht am Platz antrifft, "
               "reserviert direkt am Display einen freien Termin und erhält eine Einladung. "
               "Der Agent ist das Programm auf dem Rechner, das diese Informationen ermittelt und per "
               "USB-Kabel an das Display schickt."))
    s.append(Spacer(1, 3 * mm))
    s.append(diagram())

    s.append(P("2. Was der Agent macht", st_h2))
    s.append(table([
        ["Funktion", "Quelle", "Daten, die gelesen werden", "Intervall"],
        ["Anruf erkennen", "Windows-Registry (HKCU), nur lesen: Mikrofon-Nutzungsanzeige von Windows "
         "(CapabilityAccessManager, ConsentStore microphone)", "nur ob eine App gerade das Mikrofon benutzt und "
         "welche (z.&nbsp;B. Teams, 3CX). <b>Keine Audiodaten.</b>", "2 s"],
        ["Bildschirmsperre", "Windows-API WTSQuerySessionInformation", "nur gesperrt ja oder nein", "2 s"],
        ["Teams-Status", "Microsoft Graph GET /me/presence", "Verfügbarkeit und Aktivität "
         "(z.&nbsp;B. Busy, InACall)", "10 s"],
        ["Kalender", "Microsoft Graph GET /me/calendarView, heute und morgen",
         "nur <b>Beginn, Ende, Frei/Gebucht-Status, abgesagt</b> (Feldauswahl $select=showAs,start,end,"
         "isCancelled,isAllDay). Kein Betreff, kein Ort, keine Teilnehmer, kein Inhalt.", "60 s"],
        ["Terminbuchung (Kernfunktion)", "Microsoft Graph GET /me/calendarView (2 Wochen) und POST /me/events",
         "liest dieselben Zeitfelder. Legt auf Anfrage vom Display einen Termin im eigenen Kalender an, mit "
         "Titel und E-Mail-Adresse, die der Besucher eingegeben hat. Exchange verschickt dabei die Einladung "
         "an den Besucher.", "nur bei Buchung"],
        ["Ausgabe ans Display", "USB-Seriell (COM-Port, 115200 Baud)", "Status, Uhrzeit, «frei ab» bzw. «frei bis», "
         "Terminblöcke von heute und morgen (nur Zeiten), Name des Kontos", "2 bis 30 s"],
    ], [27 * mm, 47 * mm, 82 * mm, 18 * mm]))

    s.append(KeepTogether([P("3. Was der Agent nicht macht", st_h2)] + bullets([
        "<b>Keine Audio- oder Videoaufnahme.</b> Er öffnet weder Mikrofon noch Kamera, er liest nur die "
        "Windows-Anzeige, ob sie benutzt werden.",
        "<b>Liest keine E-Mails, Kontakte, Dateien, Chats</b> und keine Betreffs, Orte, Teilnehmer oder "
        "Inhalte von Terminen.",
        "<b>Nur das eigene Postfach</b> (/me). Kein Zugriff auf Kalender oder Status anderer Personen.",
        "<b>Keine eingehenden Verbindungen</b>, kein offener Port, kein Webserver auf dem Rechner.",
        "<b>Ausgehend nur</b> login.microsoftonline.com und graph.microsoft.com (HTTPS 443). Keine Telemetrie, "
        "keine Daten an Dritte oder an den Entwickler.",
        "<b>Keine Administratorrechte</b>, kein Windows-Dienst, keine eigenen Treiber, keine Änderungen an "
        "System- oder Sicherheitseinstellungen.",
        "<b>Keine Tastatur- oder Bildschirmaufzeichnung</b>, keine Fernsteuerung.",
        "<b>Führt nichts aus, was vom Display kommt.</b> Er versteht nur vier feste Textmeldungen: "
        "Anmeldung starten, freie Zeiten abfragen, Termin buchen, Protokollzeile. Alles andere wird ignoriert.",
        "<b>Das Display meldet sich nur als COM-Port.</b> Windows sieht einen COM-Port und die "
        "JTAG-Debugschnittstelle des Display-Chips, keine Tastatur (HID) und keinen Massenspeicher. "
        "Der Chip des Displays (ESP32-P4) könnte an diesem Anschluss grundsätzlich auch als anderes "
        "USB-Gerät auftreten (USB-OTG). Die Tally32-Firmware schaltet das nicht ein.",
    ])))

    s.append(P("4. Technische Angaben", st_h2))
    s.append(table([
        ["Merkmal", "Angabe"],
        ["Programm", "Python-Skript agent/tally_agent.py (Quellcode einsehbar), Python 3.11 oder neuer. "
         "Alternativ als einzelne .exe verpackbar (PyInstaller), falls Python nicht erlaubt ist."],
        ["Abhängigkeiten", "msal und msal-extensions (Microsoft Authentication Library, von Microsoft) und "
         "pyserial, aus PyPI. msal bringt requests, cryptography und PyJWT mit. Alle Versionen sind in "
         "agent/requirements.txt fest vorgegeben (z.&nbsp;B. msal==1.39.0)."],
        ["Ausführung", "im Benutzerkontext, unsichtbar (pythonw.exe), gestartet über die Aufgabenplanung "
         "«bei Anmeldung», Aufgabe «Tally32 Agent». Eingerichtet mit install_windows.ps1 "
         "ohne erhöhte Rechte."],
        ["Dateien", "Programm und Python-Umgebung im Projektordner. Einstellungen in "
         "%USERPROFILE%\\.config\\tally32\\config.json. Anmeldetoken mit <b>Windows DPAPI "
         "verschlüsselt</b> in token_cache.bin daneben, nur unter dem eigenen Benutzerkonto lesbar "
         "(macOS: Schlüsselbund). Ein Token aus einer älteren Version wird beim Start übernommen und "
         "die unverschlüsselte Datei gelöscht. Log in "
         "%LOCALAPPDATA%\\tally32\\tally32.log (Statusänderungen und Buchungen mit Datum, Zeit, "
         "E-Mail-Adresse und Titel des Besuchers)."],
        ["USB-Gerät", "GUITION JC1060P470C 7\" oder GUITION JC8012P4A1C 10,1\" (beide ESP32-P4), "
         "eingebaute USB-Schnittstelle des Chips "
         "(USB-Serial/JTAG, USB-VID 303A, PID 1001): COM-Port und JTAG-Debugschnittstelle des "
         "Displays. Treiber in Windows enthalten."],
        ["Netzwerk", "ausgehend HTTPS 443 zu login.microsoftonline.com und graph.microsoft.com. "
         "Bei der Einrichtung einmalig pypi.org für die Pakete (entfällt mit der .exe)."],
        ["Last", "sehr gering: rund 7 kleine HTTPS-Abfragen pro Minute, wenige MB RAM."],
    ], [30 * mm, 144 * mm]))

    s.append(P("5. Berechtigungen in Microsoft 365", st_h2))
    s.append(table([
        ["Recht (delegiert)", "Wozu", "Nötig"],
        ["Calendars.ReadBasic", "Frei/Gebucht-Zeiten des eigenen Kalenders. Kleinstes Leserecht für "
         "calendarView laut Microsoft, gibt keinen Termintext und keine Anhänge frei. Der Agent fordert davon "
         "nur die Zeitfelder an. Keine Admin-Zustimmung nötig.", "ja"],
        ["Presence.Read", "eigener Teams-Status", "optional (abschaltbar)"],
        ["Calendars.ReadWrite", "Terminbuchung am Display: Termin im eigenen Kalender anlegen, mit "
         "Einladung an den Besucher. Kernfunktion, im Agenten eingeschaltet (\"booking\": true).", "ja"],
    ], [38 * mm, 106 * mm, 30 * mm]))
    s.append(Spacer(1, 2 * mm))
    s.append(P("Anmeldung per Device Code mit dem eigenen Konto, delegiert, also nie mehr Rechte als der "
               "Benutzer selbst hat. Standardmässig über den öffentlichen Microsoft-Client "
               "«Microsoft Graph Command Line Tools» (App-ID 14d82eec-204b-4c2f-b7e8-296a70dab67e). Auf "
               "Wunsch der ICT stattdessen über eine eigene App-Registrierung des Tenants (nur die App-ID "
               "wird geändert). Dafür braucht es einmal die Zustimmung eines Admins (Abschnitt 10).", st_body))

    s.append(P("6. Das Display", st_h2))
    s.extend(bullets([
        "speichert <b>kein Microsoft-Token</b> und keine Kalenderinhalte, nur die Terminblöcke von heute und "
        "morgen (Zeiten ohne Text, höchstens 16 Stunden gültig), damit es auch abgedockt weiter anzeigt.",
        "spannt für die Einrichtung ein <b>eigenes WLAN</b> auf (Tally32-XXXX, WPA2 mit Passwort) mit "
        "passwortgeschütztem Webinterface. Dieses WLAN hat keine Verbindung ins Firmennetz und kein "
        "Internet. Gespeichert sind dort nur Einstellungen, das WLAN-Passwort und der Hash des Web-Passworts. "
        "Das Einrichtungs-WLAN lässt sich im Webinterface oder im Menü am Display <b>abschalten</b>. Wieder einschalten geht nur am "
        "Gerät selbst (Einstellungsmenü mit PIN oder 5 Sekunden aufs Display drücken).",
        "zeigt am Platz nur Status und Zeiten, <b>nie Betreffs</b> von Terminen.",
    ]))

    s.append(P("7. Risikobeurteilung", st_h2))
    s.append(table([
        ["Risiko", "Eintritt", "Auswirkung", "Massnahmen", "Rest"],
        ["Schadsoftware unter dem Benutzerkonto liest das Anmeldetoken", "gering", "mittel: eigener Kalender "
         "lesbar, mit Buchung auch beschreibbar, bis zum Widerruf",
         "Token mit Windows DPAPI verschlüsselt (nur unter dem eigenen Konto lesbar), nur delegierte Rechte, "
         "jederzeit in Entra widerrufbar. Schreibrecht nur auf den eigenen Kalender, jede Buchung im Log "
         "des Agenten nachvollziehbar.", "gering"],
        ["Missbrauch der Buchung am Display (unerwünschte Termine oder Einladungen)", "mittel", "gering",
         "nur freie Zeiten im eingestellten Zeitfenster, höchstens 4 Buchungen pro Stunde, Einladung "
         "nur an die eingegebene Adresse. Zeitfenster, Wochentage und Mittagspause im Setup. Jede Buchung "
         "steht im Kalender und im Log, die Funktion lässt sich bei Bedarf am Display abschalten.", "gering"],
        ["Zusätzliches WLAN im Büro durch das Display", "sicher", "gering",
         "WPA2 mit eigenem Passwort, kein Routing ins Firmennetz, Webinterface mit Passwort und "
         "Pflicht-Passwortwechsel. Einrichtungs-WLAN im Webinterface abschaltbar, wieder einschalten nur "
         "am Gerät.", "gering"],
        ["Manipuliertes Display oder USB-Gerät", "gering", "gering",
         "Agent nimmt nur vier feste Textmeldungen an, keine Codeausführung. Das Display meldet sich nur "
         "als COM-Port und JTAG-Schnittstelle, keine Tastatur. Eine andere Firmware könnte das ändern, "
         "das setzt aber physischen Zugriff auf das Display voraus.", "gering"],
        ["Abhängigkeiten aus PyPI (Lieferkette)", "gering", "mittel",
         "nur drei verbreitete Pakete (msal und msal-extensions von Microsoft, pyserial). Alle Versionen "
         "inklusive indirekter Pakete festgeschrieben, wahlweise eine geprüfte .exe.", "gering"],
        ["Anmeldung per Device Code (bekanntes Phishing-Muster)", "gering", "gering",
         "Anmeldung startet nur der Benutzer selbst, das Token landet auf dem eigenen Rechner. Sperrt Conditional "
         "Access den Device Code Flow, ist eine eigene App-Registrierung nötig.", "gering"],
        ["Datenschutz: Verfügbarkeit sichtbar am Platz", "beabsichtigt", "gering",
         "nur Status und Zeiten, keine Betreffs, entspricht dem Teams-Status. Im Bildschirmschoner ist nur "
         "das Logo zu sehen.", "gering"],
    ], [40 * mm, 21 * mm, 29 * mm, 70 * mm, 14 * mm]))
    s.append(Spacer(1, 3 * mm))
    s.append(box("<b>Gesamtbeurteilung: geringes Risiko.</b> Der Agent arbeitet mit den Rechten des Benutzers, "
                 "liest nur Zeiten und Status des eigenen Kontos, öffnet keine Ports und spricht nur mit "
                 "Microsoft. Geschrieben wird nur für die Terminbuchung und nur in den eigenen Kalender. "
                 "Das Token liegt verschlüsselt im Benutzerprofil."))

    s.append(P("8. Schutzmassnahmen und Hinweise für den Betrieb", st_h2))
    s.extend(bullets([
        "Anmeldetoken verschlüsselt ablegen (Windows DPAPI). <i>Umgesetzt.</i>",
        "Paketversionen festschreiben (agent/requirements.txt). <i>Umgesetzt.</i>",
        "Einrichtungs-WLAN des Displays mit eigenem Passwort versehen und nach der Einrichtung abschalten "
        "(Webinterface, Abschnitt Einrichtungs-WLAN). <i>Umgesetzt.</i>",
        "Wahlweise eigene App-Registrierung des Tenants statt des öffentlichen Microsoft-Clients.",
        "In einer allfälligen USB-Gerätekontrolle nur das Gerät VID 303A / PID 1001 freigeben, und dort "
        "nur die Klassen COM-Port (CDC) und JTAG, keine Tastatur (HID).",
    ]))

    s.append(P("9. Widerruf und Deinstallation", st_h2))
    s.extend(bullets([
        "Anmeldung löschen: <font face='Courier'>tally_agent.py logout</font> (löscht das gespeicherte Token).",
        "Zugriff serverseitig sperren: in Entra die Anmeldesitzungen des Benutzers widerrufen oder die "
        "Zustimmung für die App entfernen.",
        "Entfernen: <font face='Courier'>install_windows.ps1 -Uninstall</font> (Aufgabe und Prozess), danach "
        "Projektordner und %USERPROFILE%\\.config\\tally32 löschen.",
    ]))

    s.append(PageBreak())
    s.append(P("10. Warum einmal eine Admin-Freigabe n\u00f6tig ist", st_h2))
    s.append(P("Auf dem Rechner braucht der Agent <b>keine Adminrechte</b>. Einmalig n\u00f6tig ist aber eine "
               "<b>Zustimmung (Consent) in Microsoft Entra ID</b>, damit der Agent im Namen des Benutzers auf "
               "dessen eigenen Kalender und Teams-Status zugreifen darf.", st_body))
    s.append(Spacer(1, 2 * mm))
    s.append(table([
        ["Was", "Administratorzustimmung f\u00fcr die App \u00abMicrosoft Graph Command Line Tools\u00bb "
                "(oder eine eigene App-Registrierung) mit den delegierten Rechten Calendars.ReadBasic, "
                "Calendars.ReadWrite und Presence.Read"],
        ["Warum", "Viele Tenants erlauben Benutzern nicht, Apps selbst zuzustimmen, und Schreibrechte auf den "
                  "Kalender gibt meist nur ein Admin frei. Ohne Zustimmung bricht die Anmeldung mit "
                  "\u00abAdministratorgenehmigung erforderlich\u00bb ab."],
        ["Wie oft", "einmalig. Danach meldet sich der Benutzer selbst an, der Agent erneuert seine Tokens ohne "
                    "weitere Freigabe."],
        ["Was sie nicht bedeutet", "keine Adminrechte auf dem Rechner, kein Zugriff auf andere Postf\u00e4cher "
                                   "(delegiert, nur das eigene Konto), keine Anwendungsberechtigung (App-only), "
                                   "kein Zugriff auf E-Mails, Dateien oder Chats"],
        ["Eingrenzen", "Die Zustimmung l\u00e4sst sich auf diesen einen Benutzer beschr\u00e4nken statt auf die "
                       "ganze Organisation (OAuth2-Berechtigung mit consentType Principal, siehe unten). "
                       "Alternativ eine eigene App-Registrierung, die nur diesem Benutzer zugewiesen ist."],
        ["R\u00fcckg\u00e4ngig", "in Entra unter Unternehmensanwendungen die Berechtigung entfernen, gilt sofort"],
    ], [36 * mm, 138 * mm], head=False))
    s.append(Spacer(1, 2 * mm))
    s.append(P("Zustimmung nur f\u00fcr einen Benutzer (Microsoft Graph PowerShell, Platzhalter ersetzen):", st_small))
    s.append(P("<font face='Courier' size='7.5'>New-MgOauth2PermissionGrant -BodyParameter @{<br/>"
               "&nbsp;&nbsp;clientId&nbsp;&nbsp;&nbsp;&nbsp;= \"&lt;Objekt-ID Service Principal der App&gt;\"<br/>"
               "&nbsp;&nbsp;consentType = \"Principal\"<br/>"
               "&nbsp;&nbsp;principalId = \"&lt;Objekt-ID des Benutzers&gt;\"<br/>"
               "&nbsp;&nbsp;resourceId&nbsp;&nbsp;= \"&lt;Objekt-ID Service Principal Microsoft Graph&gt;\"<br/>"
               "&nbsp;&nbsp;scope&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; = \"Calendars.ReadBasic Calendars.ReadWrite Presence.Read\"<br/>"
               "}</font>", st_body))

    s.append(P("11. Auf einen Blick", st_h2))
    s.append(table([
        ["Punkt", "Angabe"],
        ["Programm", "Tally32 Agent, Python-Skript, l\u00e4uft unter dem eigenen Benutzerkonto ohne Adminrechte"],
        ["Liest", "eigenen Teams-Status, Zeiten und Frei/Gebucht des eigenen Kalenders, ob das Mikrofon "
                  "benutzt wird, ob der Bildschirm gesperrt ist"],
        ["Liest nicht", "E-Mails, Dateien, Chats, Kontakte, Betreffs, Teilnehmer und Inhalte von Terminen, Audio"],
        ["Schreibt", "Terminbuchung (Kernfunktion): Termine im eigenen Kalender mit Einladung an den Besucher"],
        ["Netzwerk", "nur ausgehend HTTPS zu login.microsoftonline.com und graph.microsoft.com, kein offener Port"],
        ["Rechte Microsoft 365", "delegiert: Calendars.ReadBasic, Calendars.ReadWrite (Terminbuchung), "
                                 "Presence.Read, einmalige Admin-Zustimmung"],
        ["Ger\u00e4t", "Display per USB (nur COM-Port und JTAG des Displays), eigenes Einrichtungs-WLAN mit WPA2, abschaltbar"],
        ["Gesamtrisiko", "<b>gering</b> (Abschnitt 7)"],
        ["Widerruf", "jederzeit: Token l\u00f6schen, Sitzungen in Entra widerrufen, Aufgabe entfernen"],
    ], [38 * mm, 136 * mm]))
    return s


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm,
                            bottomMargin=16 * mm, title="Tally32 Agent: Spec Sheet für die ICT",
                            author="Oliver Timme", subject="Lokaler Client, Funktionsumfang und Risikobeurteilung")
    doc.build(build(), onFirstPage=on_page, onLaterPages=on_page)
    print("PDF:", OUT)


if __name__ == "__main__":
    main()
