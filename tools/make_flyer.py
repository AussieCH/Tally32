#!/usr/bin/env python3
"""
make_flyer.py - Flyer Tally32 Basic (A4, zwei Seiten) -> docs/Tally32_Basic_Flyer.pdf

Vorderseite: Was es ist, Bildschirmfotos, Funktionen. Rückseite: Stückliste (BOM),
Einrichtung in vier Schritten, Voraussetzungen, Lizenz und Kontakt.
Erzeugen mit ~/.pdf-venv/bin/python tools/make_flyer.py, Bildschirmfotos aus docs/screens.
"""
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
OUT = os.path.join(ROOT, "docs", "Tally32_Basic_Flyer.pdf")
SCREENS = os.path.join(ROOT, "docs", "screens")
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
    p = os.path.join(SCREENS, name)
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
    c.drawString(16 * mm, A4[1] - 25 * mm, "Die Statusanzeige für den Arbeitsplatz. Im Gespräch, besetzt bis, frei ab.")
    c.setFont("Montserrat", 9)
    c.drawRightString(A4[0] - 16 * mm, A4[1] - 15 * mm, "Nicht kommerzielle Lizenz  ·  Stand " + DATE)
    c.setFillColor(GREY)
    c.setFont("Helvetica", 7.5)
    c.drawString(16 * mm, 9 * mm, "Timme Security  ·  hello@timme-security.ch  ·  github.com/AussieCH/Tally32")
    c.drawRightString(A4[0] - 16 * mm, 9 * mm, "Seite %d von 2" % doc.page)
    c.restoreState()


def build():
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

    doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=16 * mm, rightMargin=16 * mm, topMargin=42 * mm,
                            bottomMargin=16 * mm, title="Tally32 Basic Flyer", author="Oliver Timme")
    doc.build(s, onFirstPage=on_page, onLaterPages=on_page)
    print("PDF:", OUT)


if __name__ == "__main__":
    build()
