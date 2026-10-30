#!/usr/bin/env python3
"""
make_flyer.py - Flyer Tally32 Basic (A4, zwei Seiten) -> docs/Tally32_Basic_Flyer.pdf

Vorderseite: Was es ist, Bildschirmfotos, Funktionen. Rückseite: Stückliste (BOM),
Einrichtung in vier Schritten, Voraussetzungen, Lizenz und Kontakt.
Erzeugen mit ~/.pdf-venv/bin/python tools/make_flyer.py, Bildschirmfotos aus docs/screens.
Englische Fassung: --lang en -> docs/Tally32_Basic_Flyer_EN.pdf, Bildschirmfotos aus docs/screens/en.
"""
import argparse
import datetime as dt
import os

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Image, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = {"de": os.path.join(ROOT, "docs", "Tally32_Basic_Flyer.pdf"),
       "en": os.path.join(ROOT, "docs", "Tally32_Basic_Flyer_EN.pdf")}
SCREENS = {"de": os.path.join(ROOT, "docs", "screens"), "en": os.path.join(ROOT, "docs", "screens", "en")}
LANG = "de"


def L(de, en):
    """Text in der gewählten Sprache."""
    return en if LANG == "en" else de
DATE = dt.date.today().strftime("%m/%Y")

pdfmetrics.registerFont(TTFont("Montserrat", os.path.join(ROOT, "tools/fonts/Montserrat-Medium.ttf")))
PURPLE, LIGHT, GREY, LINE, YELLOW = (colors.HexColor(c) for c in ("#432078", "#F1ECFB", "#5F5F66", "#D9D6CC", "#D2BA62"))

H1 = ParagraphStyle("h1", fontName="Montserrat", fontSize=26, leading=30, textColor=colors.white)
SUB = ParagraphStyle("sub", fontName="Montserrat", fontSize=12, leading=15, textColor=colors.white)
H2 = ParagraphStyle("h2", fontName="Montserrat", fontSize=13.5, leading=17, textColor=PURPLE, spaceBefore=7, spaceAfter=3)
BODY = ParagraphStyle("body", fontName="Helvetica", fontSize=9.6, leading=12.6, spaceAfter=3)
SMALL = ParagraphStyle("small", parent=BODY, fontSize=8, leading=10.4, textColor=GREY)
BUL = ParagraphStyle("bul", parent=BODY, leftIndent=9, bulletIndent=0, spaceAfter=1.5)
CELL = ParagraphStyle("cell", parent=BODY, fontSize=8.6, leading=11, spaceAfter=0)
CELLB = ParagraphStyle("cellb", parent=CELL, fontName="Helvetica-Bold")
HEAD = ParagraphStyle("head", parent=CELL, fontName="Helvetica-Bold", textColor=colors.white)
CAP = ParagraphStyle("cap", parent=SMALL, alignment=1, spaceBefore=1)


def P(t, s=BODY):
    return Paragraph(t, s)


def bullets(items):
    return [Paragraph(t, BUL, bulletText="•") for t in items]


def table(rows, widths, head=True, bold_first=True):
    data = [[Paragraph(c, HEAD if (head and i == 0) else (CELLB if (j == 0 and bold_first) else CELL))
             for j, c in enumerate(r)] for i, r in enumerate(rows)]
    t = Table(data, colWidths=[w * mm for w in widths])
    st = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, LINE),
          ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4),
          ("TOPPADDING", (0, 0), (-1, -1), 2.5), ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5)]
    if head:
        st.append(("BACKGROUND", (0, 0), (-1, 0), PURPLE))
    for i in range(2 if head else 1, len(rows), 2):
        st.append(("BACKGROUND", (0, i), (-1, i), LIGHT))
    t.setStyle(TableStyle(st))
    return t


def shot(name, w_mm):
    p = os.path.join(SCREENS[LANG], name)
    return Image(p, width=w_mm * mm, height=w_mm * mm * 600 / 1024)


def shots(items, w_mm):
    """Bildschirmfotos nebeneinander mit Bildunterschrift."""
    imgs = [shot(f, w_mm) for f, _ in items]
    caps = [Paragraph(c, CAP) for _, c in items]
    t = Table([imgs, caps], colWidths=[(w_mm + 3) * mm] * len(items))
    t.setStyle(TableStyle([("ALIGN", (0, 0), (-1, -1), "CENTER"), ("VALIGN", (0, 0), (-1, -1), "TOP"),
                           ("LEFTPADDING", (0, 0), (-1, -1), 1.5), ("RIGHTPADDING", (0, 0), (-1, -1), 1.5),
                           ("TOPPADDING", (0, 0), (-1, -1), 1), ("BOTTOMPADDING", (0, 0), (-1, -1), 1)]))
    return t


def two_cols(left, right, widths=(88, 88)):
    t = Table([[left, right]], colWidths=[w * mm for w in widths])
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                           ("RIGHTPADDING", (0, 0), (-1, -1), 4)]))
    return t


def on_page(c, doc):
    c.saveState()
    # Kopfbanner im Logo-Violett
    c.setFillColor(PURPLE)
    c.rect(0, A4[1] - 34 * mm, A4[0], 34 * mm, fill=1, stroke=0)
    c.setFillColor(YELLOW)
    c.rect(0, A4[1] - 36 * mm, A4[0], 2 * mm, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont("Montserrat", 26)
    c.drawString(16 * mm, A4[1] - 17 * mm, "Tally32 Basic")
    c.setFont("Montserrat", 11.5)
    c.drawString(16 * mm, A4[1] - 25 * mm, L("Die Statusanzeige für den Arbeitsplatz. Im Gespräch, besetzt bis, frei ab.",
                                                    "The status display for your desk. On a call, busy until, free from."))
    c.setFont("Montserrat", 9)
    c.drawRightString(A4[0] - 16 * mm, A4[1] - 15 * mm, L("Nicht kommerzielle Lizenz  ·  Stand ", "Noncommercial licence  ·  ") + DATE)
    c.setFillColor(GREY)
    c.setFont("Helvetica", 7.5)
    c.drawString(16 * mm, 9 * mm, "Timme Security  ·  hello@timme-security.ch  ·  github.com/AussieCH/Tally32")
    c.drawRightString(A4[0] - 16 * mm, 9 * mm, L("Seite %d von 2", "Page %d of 2") % doc.page)
    c.restoreState()


def build(lang="de"):
    global LANG
    LANG = lang
    if lang == "en":
        build_en()
        return
    s = []
    # ------------------------------------------------------------ Seite 1
    s.append(P("Ein kleines Display neben dem Bildschirm zeigt Kolleginnen, Kollegen und Besuchenden auf einen Blick, "
               "ob man gerade telefoniert, in einer Sitzung sitzt oder frei ist, und <b>ab wann man wieder frei ist</b>. "
               "Die Daten kommen vom eigenen Computer per USB: Mikrofon, Outlook-Kalender und Teams-Status. "
               "Kein Server, kein Firmennetz, keine Cloud ausser dem Microsoft 365, das schon da ist."))
    s.append(Spacer(1, 2 * mm))
    s.append(shots([("status-on-air-termin.png", "ON AIR im Termin, mit frei ab"),
                    ("status-besetzt.png", "BESETZT, mit frei ab"),
                    ("status-frei.png", "FREI bis, danach wieder frei ab")], 58))
    s.append(Spacer(1, 1 * mm))
    s.append(shots([("status-abwesend.png", "ABWESEND laut Kalender"),
                    ("status-nicht-am-platz.png", "Notebook abgedockt, nächster Termin"),
                    ("status-ausser-haus.png", "Feierabend, zurück morgen ab 07:00")], 58))

    left = [P("Anzeige", H2)] + bullets([
        "<b>Sieben Zustände</b>: Im Gespräch (ON AIR), Besetzt, Abwesend, Frei, Nicht am Platz, Ausser Haus, Kein Signal.",
        "<b>Frei ab und frei bis</b> aus dem Outlook-Kalender, Lücken bis 5 Minuten zählen als durchgehend besetzt.",
        "<b>Anrufe erkennt das Mikrofon</b>: Teams, 3CX, Webex, Zoom oder jedes andere Programm, das es belegt. "
        "Dazu der Teams-Status (im Anruf, präsentiert, nicht stören, abwesend).",
        "<b>Jeder Zustand mit Text und Symbol</b>, nicht nur über die Farbe. Statusfarben sind zusätzlich in der "
        "Helligkeit getrennt, auch für Farbenblinde eindeutig.",
        "<b>Name in der Kopfzeile</b>, Farben, Helligkeit und Drehung frei einstellbar.",
        "<b>Bildschirmschoner</b> bei gesperrtem Rechner und nach Feierabend, Antippen weckt.",
    ])
    right = [P("Terminbuchung am Display", H2)] + bullets([
        "Besuchende buchen am Display selbst einen Termin: E-Mail, Titel, 15, 30 oder 60 Minuten, freie Zeit "
        "in dieser oder nächster Woche. Der Termin landet im Kalender, die Einladung geht per E-Mail.",
        "Zeitfenster, Mittagspause und Wochentage einstellbar, höchstens 4 Buchungen pro Stunde.",
    ]) + [P("Ohne Notebook", H2)] + bullets([
        "Abgedockt rechnet das Display mit den gepufferten Terminen weiter: Besetzt mit «zurück ca.», sonst "
        "Nicht am Platz mit dem nächsten Termin. Eine Echtzeituhr hält die Zeit über Stromunterbrüche.",
    ]) + [P("Einrichten und bedienen", H2)] + bullets([
        "Eigenes Einrichtungs-WLAN mit Webinterface am Handy, abschaltbar.",
        "Menü direkt am Display (PIN-geschützt), Demomodus für die Vorführung.",
        "Agent für Windows 11 und macOS mit Autostart, Anmeldung bei Microsoft 365 per Code.",
    ])
    s.append(two_cols(left, right))
    s.append(PageBreak())

    # ------------------------------------------------------------ Seite 2
    s.append(P("Stückliste (BOM)", H2))
    s.append(P("Alles, was es für einen Arbeitsplatz braucht. Ständer und Befestigung folgen separat (3D-Druck, in Arbeit)."))
    s.append(table([
        ["Nr.", "Teil", "Angaben", "Menge", "Richtpreis"],
        ["1", "Display GUITION JC1060P470C", "7 Zoll IPS 1024 x 600, Touch, ESP32-P4 mit WLAN (ESP32-C6), MIPI-DSI, "
         "Echtzeituhr, 16 MB Flash. Gehäuse schwarz, zwei USB-C-Buchsen (obere verwenden)", "1", "ca. 50 Fr."],
        ["2", "USB-C-Kabel", "Datenkabel USB 2.0 (kein reines Ladekabel), Länge nach Arbeitsplatz, 1 bis 2 m. "
         "An die Dockingstation oder ein Netzteil anschliessen, nicht direkt ans Notebook", "1", "ca. 5 Fr."],
        ["3", "Knopfzelle CR1220", "Puffer für die Echtzeituhr, damit die Uhrzeit Stromunterbrüche übersteht "
         "(z. B. beim Abdocken am HP Dock G5)", "1", "ca. 2 Fr."],
        ["4", "USB-Netzteil 5 V, 2 A (optional)", "Nur wenn die Dockingstation keinen freien USB-Anschluss hat "
         "oder das Display auch ohne Notebook leuchten soll", "0 bis 1", "ca. 10 Fr."],
        ["5", "Firmware Tally32", "Dieses Repository, Ordner firmware, flashen über USB-C ohne Knopfdruck", "", "0 Fr."],
        ["6", "Agent Tally32", "Python-Skript für Windows 11 und macOS, Ordner agent, mit Autostart-Installer", "", "0 Fr."],
    ], [9, 44, 92, 14, 19]))
    s.append(P("Richtpreise Stand 10/2026 (AliExpress bzw. Händler), Display auch als 4,3 Zoll (ca. 40 Fr.) und "
               "10 Zoll (ca. 60 Fr.) von GUITION, Firmware dafür in Vorbereitung. Summe pro Arbeitsplatz rund "
               "<b>60 Franken</b> ohne Ständer.", SMALL))

    s.append(P("Voraussetzungen", H2))
    s.append(table([
        ["Bereich", "Was es braucht"],
        ["Computer", "Windows 11 oder macOS 14.2 oder neuer, ein freier USB-Anschluss an Dock oder Rechner, Python 3.12 "
         "(ohne Adminrechte installierbar)"],
        ["Microsoft 365", "Konto mit Exchange-Postfach. Rechte: Calendars.ReadBasic, Presence.Read, für die Buchung "
         "Calendars.ReadWrite. Je nach Mandant einmalige Admin-Freigabe"],
        ["Netz", "Keines. Das Display hängt nur am USB, das Einrichtungs-WLAN ist ein eigenes Netz ohne Internet "
         "und kann abgeschaltet werden"],
        ["Sicherheit", "Kein Microsoft-Token auf dem Display, Token auf dem Rechner verschlüsselt (DPAPI bzw. "
         "Schlüsselbund). Das Display meldet sich nur als COM-Port, nie als Tastatur oder Speicher"],
    ], [34, 144]))

    s.append(P("Einrichtung in vier Schritten", H2))
    s.append(table([
        ["Schritt", "Tun", "Dauer"],
        ["1", "Firmware flashen: <font face=Courier>cd firmware && pio run -t upload</font>, Display an die obere USB-C-Buchse", "5 min"],
        ["2", "Agent installieren: <font face=Courier>install_windows.ps1</font> bzw. <font face=Courier>install_mac.sh</font>, "
         "legt Python-Umgebung und Autostart an", "5 min"],
        ["3", "Bei Microsoft 365 anmelden: <font face=Courier>tally_agent.py login</font>, Code am PC eingeben. "
         "Vorher in config.json <font face=Courier>\"booking\": true</font> für die Terminbuchung", "2 min"],
        ["4", "Am Handy mit dem WLAN <b>Tally32-XXXX</b> (Passwort tally32setup) verbinden, http://192.168.4.1, "
         "admin / tally32, Passwort ändern, Name und Farben setzen", "5 min"],
    ], [16, 142, 20]))

    s.append(P("Lizenz und Kontakt", H2))
    s.append(P("Tally32 Basic steht unter der PolyForm Noncommercial License 1.0.0: frei für private Nutzung, "
               "gemeinnützige Organisationen, Bildung, Forschung und öffentliche Stellen. Für jede kommerzielle "
               "Nutzung, etwa in einem Unternehmen, gibt es eine kommerzielle Lizenz."))
    s.append(P("Timme Security, Oliver Timme, hello@timme-security.ch. Quellcode, Anleitung und Spec Sheet für die IT: "
               "github.com/AussieCH/Tally32", SMALL))

    finish(s)


def finish(s):
    out = OUT[LANG]
    doc = SimpleDocTemplate(out, pagesize=A4, leftMargin=16 * mm, rightMargin=16 * mm, topMargin=42 * mm,
                            bottomMargin=16 * mm, title="Tally32 Basic Flyer", author="Oliver Timme")
    doc.build(s, onFirstPage=on_page, onLaterPages=on_page)
    print("PDF:", out)


def build_en():
    """Englische Fassung, Aufbau wie build()."""
    s = []
    # ------------------------------------------------------------ Page 1
    s.append(P("A small display next to your monitor shows colleagues and visitors at a glance whether you are on a call, "
               "in a meeting or free, and <b>when you will be free again</b>. "
               "The data comes from your own computer over USB: microphone, Outlook calendar and Teams status. "
               "No server, no company network, no cloud beyond the Microsoft 365 you already have."))
    s.append(Spacer(1, 2 * mm))
    s.append(shots([("status-on-air-termin.png", "ON AIR in a meeting, with free from"),
                    ("status-besetzt.png", "BUSY, with free from"),
                    ("status-frei.png", "FREE until, then free from")], 58))
    s.append(Spacer(1, 1 * mm))
    s.append(shots([("status-abwesend.png", "AWAY according to the calendar"),
                    ("status-nicht-am-platz.png", "Laptop undocked, next appointment"),
                    ("status-ausser-haus.png", "Day is over, back tomorrow at 07:00")], 58))

    left = [P("Display", H2)] + bullets([
        "<b>Seven states</b>: ON A CALL (ON AIR), BUSY, AWAY, FREE, NOT AT DESK, OUT OF OFFICE, NO SIGNAL.",
        "<b>Free from and free until</b> from the Outlook calendar. Gaps of up to 5 minutes count as busy.",
        "<b>The microphone detects calls</b>: Teams, 3CX, Webex, Zoom or any other app that uses it. "
        "Plus the Teams status (in a call, presenting, do not disturb, away).",
        "<b>Every state with text and symbol</b>, not just colour. Status colours also differ in "
        "brightness, so they stay clear for colour blind people.",
        "<b>Bilingual</b>: German or English, switchable in the menu (Sprache / Language).",
        "<b>Your name in the header</b>, colours, brightness and rotation adjustable.",
        "<b>Screen saver</b> when the computer is locked and after work. A tap wakes it.",
    ])
    right = [P("Book a meeting on the display", H2)] + bullets([
        "Visitors book a meeting on the display themselves: email, title, 15, 30 or 60 minutes, a free slot "
        "this week or next. The meeting goes into your calendar and the invitation is sent by email.",
        "Time window, lunch break and weekdays adjustable, at most 4 bookings per hour.",
    ]) + [P("Without the laptop", H2)] + bullets([
        "When undocked, the display keeps working from the buffered appointments: BUSY with "
        "\u201cback approx.\u201d, otherwise NOT AT DESK with the next appointment. A real time clock keeps "
        "the time through power cuts.",
    ]) + [P("Setup and use", H2)] + bullets([
        "Own setup Wi-Fi with a web interface for your phone, can be turned off.",
        "Menu right on the display (PIN protected), demo mode for presentations.",
        "Agent for Windows 11 and macOS with autostart, Microsoft 365 sign-in by code.",
    ])
    s.append(two_cols(left, right))
    s.append(PageBreak())

    # ------------------------------------------------------------ Page 2
    s.append(P("Bill of materials (BOM)", H2))
    s.append(P("Everything you need for one desk. Stand and mounting will follow separately (3D print, in progress)."))
    s.append(table([
        ["No.", "Part", "Details", "Qty", "Approx. price"],
        ["1", "Display GUITION JC1060P470C", "7 inch IPS 1024 x 600, touch, ESP32-P4 with Wi-Fi (ESP32-C6), MIPI-DSI, "
         "real time clock, 16 MB flash. Black case, two USB-C ports (use the upper one)", "1", "approx. CHF 50"],
        ["2", "USB-C cable", "USB 2.0 data cable (not a charge-only cable), length to suit the desk, 1 to 2 m. "
         "Connect to the docking station or a power supply, not directly to the laptop", "1", "approx. CHF 5"],
        ["3", "Coin cell CR1220", "Backup for the real time clock, so the time survives power cuts "
         "(e.g. when undocking from an HP Dock G5)", "1", "approx. CHF 2"],
        ["4", "USB power supply 5 V, 2 A (optional)", "Only if the docking station has no free USB port "
         "or the display should stay on without the laptop", "0 to 1", "approx. CHF 10"],
        ["5", "Tally32 firmware", "This repository, folder firmware, flashed over USB-C without pressing a button", "", "CHF 0"],
        ["6", "Tally32 agent", "Python script for Windows 11 and macOS, folder agent, with autostart installer", "", "CHF 0"],
    ], [9, 42, 84, 13, 30]))
    s.append(P("Approximate prices as of 10/2026 (AliExpress or retailers). GUITION also makes the display in 4.3 inch "
               "(approx. CHF 40) and 10 inch (approx. CHF 60), firmware for these is in preparation. Total per desk "
               "around <b>CHF 60</b> without stand.", SMALL))

    s.append(P("Requirements", H2))
    s.append(table([
        ["Area", "What you need"],
        ["Computer", "Windows 11 or macOS 14.2 or later, a free USB port on the dock or computer, Python 3.12 "
         "(installs without admin rights)"],
        ["Microsoft 365", "Account with an Exchange mailbox. Permissions: Calendars.ReadBasic, Presence.Read, for "
         "booking Calendars.ReadWrite. Depending on the tenant, a one-time admin consent"],
        ["Network", "None. The display only connects over USB. The setup Wi-Fi is a separate network without "
         "internet and can be turned off"],
        ["Security", "No Microsoft token on the display, token on the computer encrypted (DPAPI or "
         "Keychain). The display only shows up as a COM port, never as a keyboard or storage"],
    ], [34, 144]))

    s.append(P("Setup in four steps", H2))
    s.append(table([
        ["Step", "What to do", "Time"],
        ["1", "Flash the firmware: <font face=Courier>cd firmware && pio run -t upload</font>, display on the upper USB-C port", "5 min"],
        ["2", "Install the agent: <font face=Courier>install_windows.ps1</font> or <font face=Courier>install_mac.sh</font>, "
         "sets up the Python environment and autostart", "5 min"],
        ["3", "Sign in to Microsoft 365: <font face=Courier>tally_agent.py login</font>, enter the code on the PC. "
         "For booking, first set <font face=Courier>\"booking\": true</font> in config.json", "2 min"],
        ["4", "On your phone, join the Wi-Fi <b>Tally32-XXXX</b> (password tally32setup), open http://192.168.4.1, "
         "admin / tally32, change the password, set name and colours", "5 min"],
    ], [16, 142, 20]))

    s.append(P("Licence and contact", H2))
    s.append(P("Tally32 Basic is licensed under the PolyForm Noncommercial License 1.0.0. It is free for private use, "
               "non-profit organisations, education, research and public bodies. Any commercial use, for example "
               "in a company, needs a commercial licence."))
    s.append(P("Timme Security, Oliver Timme, hello@timme-security.ch. Source code, guide and spec sheet for IT: "
               "github.com/AussieCH/Tally32", SMALL))
    finish(s)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Tally32 Basic Flyer")
    ap.add_argument("--lang", choices=("de", "en"), default="de")
    build(ap.parse_args().lang)
