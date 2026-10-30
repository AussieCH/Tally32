# Tally32

Statusanzeige für den Arbeitsplatz auf einem ESP32 Display (GUITION 7" oder 10,1"),
angeschlossen per USB. Zeigt wie ein Tally Light im Studio, ob man gerade telefoniert,
und aus dem Outlook Kalender, ob man besetzt ist und **ab wann man wieder frei ist**.

**Ausführliche Anleitung mit Bildern: [docs/Anleitung.md](docs/Anleitung.md)**

| Zustand | Anzeige | Auslöser |
|---|---|---|
| ON AIR / IM GESPRÄCH | "ON AIR" pulsierend über der Zeit, darunter der normale Zustand mit seinen Zeiten (z. B. BESETZT "frei ab", FREI "frei bis"), links "im Gespräch" mit Quelle (Teams, 3CX …) | Mikrofon belegt oder Teams `InACall` / `Presenting` |
| BESETZT | Kalender Symbol, "frei ab 14:30" | Termin mit `showAs` busy, oder Teams "Nicht stören" |
| ABWESEND | Koffer, "zurück ab 17:00" | Termin mit `showAs` oof oder workingElsewhere, oder Teams Abwesend / Gleich zurück |
| FREI | Häkchen, "frei bis 15:00" (nächster Termin), darunter "danach wieder frei ab 16:00" | sonst |
| NICHT AM PLATZ | Ampelmännchen, "nächster Termin 13:00", "Stand 09:29" | Notebook weg, laut Kalender gerade kein Termin |
| AUSSER HAUS | Koffer, "Feierabend" oder "Arbeitsfreier Tag", "zurück morgen ab 07:00" | ausserhalb der Arbeitszeit (frühester Beginn, spätestes Ende und Wochentage aus den Buchungseinstellungen), ersetzt FREI und NICHT AM PLATZ. Nach 2 Minuten ohne Berührung Bildschirmschoner |
| KEIN SIGNAL | durchgestrichener Kreis | keine Daten vom Agent und kein gültiger Offline-Puffer |

### So sieht es aus

Bildschirmfotos vom Gerät (10", Demomodus, Beispielkonto jasmin.muster@mail.ch, erzeugt mit
`tools/screenshot.py --demo`, das Beispielkonto setzt die Variable `TALLY32_SHOT_NAME`):

| | |
|---|---|
| ![FREI](docs/screens/status-frei.png) | ![BESETZT](docs/screens/status-besetzt.png) |
| ![ON AIR im Termin](docs/screens/status-on-air-termin.png) | ![ON AIR spontan](docs/screens/status-on-air-spontan.png) |
| ![ABWESEND](docs/screens/status-abwesend.png) | ![NICHT AM PLATZ](docs/screens/status-nicht-am-platz.png) |
| ![AUSSER HAUS](docs/screens/status-ausser-haus.png) | ![Buchung Beginn](docs/screens/buchung-5-beginn.png) |

Alle Bilder liegen in [docs/screens](docs/screens).

### Ohne Notebook (abgedockt)

Der Agent schickt alle 30 Sekunden die Uhrzeit und die Terminblöcke von heute
und morgen ans Display. Wird das Notebook abgesteckt (oder schläft es), rechnet
das Display damit selbst weiter: in einem Termin BESETZT mit "zurück ca."
bzw. ABWESEND mit "zurück ab", sonst NICHT AM PLATZ mit dem nächsten Termin.
Dazu steht immer "Stand HH:MM", also wann die Daten zuletzt aktualisiert wurden.
Der Knopf "Termin buchen" ist ohne Notebook ausgeblendet. Die Daten gelten
höchstens 16 Stunden, nach einem Stromausfall zeigt das Display KEIN SIGNAL,
bis das Notebook wieder verbunden ist.

Wichtig: Das Display an die Dockingstation oder an ein eigenes USB-Netzteil
hängen, nicht direkt ans Notebook, sonst ist es beim Abdocken aus. Manche Docks
(z. B. HP USB-C Dock G5) schalten beim Abdocken kurz den Strom ab, das Display
startet dann neu. Die Terminblöcke speichert es deshalb im Flash, die Uhrzeit hält
die Echtzeituhr auf dem Board. Dafür muss im Halter eine **Knopfzelle CR1220**
stecken, sonst zeigt das Display nach so einem Neustart KEIN SIGNAL. Ob die Uhr
läuft, steht im Menü unter Info ("Echtzeituhr").

Aufbau: hellgraue Kopfzeile mit Logo, Datum, Name und Uhrzeit. Darunter weiss, links
Symbol und Zustand (und der Knopf "Termin buchen"), in der Mitte eine feine Linie mit
einem schmalen Balken in der Statusfarbe (frei grün, im Gespräch rot, besetzt orange,
abwesend dunkelblau, kein Signal grau), rechts die Zeit. Oben rechts das Symbol für die
Verbindung zum Agenten.

Jeder Zustand ist über Text und Symbol erkennbar, nicht nur über die Farbe.
"frei ab" ist das Ende des zusammenhängenden Terminblocks. Lücken bis 5 Minuten
zählen als durchgehend besetzt.

Ein Display pro Rechner, der Agent auf dem Computer liefert alles per USB. Kein
Firmennetz, kein Server.

## Aufbau

```
Mikrofon (CoreAudio / Windows Registry) ┐
Microsoft Graph: Kalender + Presence    ┼→ agent/tally_agent.py → USB Seriell → firmware/ (GUITION 7" oder 10")
```

- `firmware/` PlatformIO Projekt für das GUITION JC1060P470C (ESP32-P4 mit ESP32-C6
  für WLAN, 7" IPS 1024x600 mit Touch, Umgebung `guition-p4`) und das GUITION JC8012P4A1C
  (10,1" IPS 1280x800, Touch GSL3680, Umgebung `guition-p4-10`), quer. Einzige App auf dem ESP32-P4
  (Partition `factory`), startet direkt nach dem Einschalten.

Bezugsquellen der Displays (AliExpress): [GUITION JC1060P470C, 7"](https://de.aliexpress.com/item/1005010023056143.html),
[GUITION JC8012P4A1C, 10,1"](https://de.aliexpress.com/item/1005009147577522.html).

Am Rechner bzw. an der Dockingstation hängt das Display an der **oberen** der
beiden USB-C-Buchsen auf der Rückseite (eingebaute USB-Schnittstelle des ESP32-P4,
Windows zeigt "Serielles USB-Gerät"). An der unteren meldet sich kein Gerät.
- `agent/` Python Agent für macOS und Windows.

## Webinterface

Das Display spannt ein eigenes WLAN auf (WPA2, ohne Internet):

| | Benutzer | Passwort |
|---|---|---|
| WLAN `Tally32-XXXX` | | `tally32setup` |
| Webinterface http://192.168.4.1 | `admin` | `tally32`, muss beim ersten Login geändert werden |

Funktionen: neue Office 365 Anmeldung auslösen (Code und Link erscheinen im
Webinterface und auf dem Display, der Agent auf dem Computer erledigt den Rest),
Farben pro Zustand, Name in der Kopfzeile (wessen Kalender das Display zeigt, leer =
angemeldetes Konto, auch im Menü am Gerät unter Anzeige änderbar), Helligkeit und Drehung, WLAN-Passwort, Web-Passwort.
Die Schriftfarbe wählt das Display je nach Helligkeit des Hintergrunds selbst.

### Menü am Display

Die meisten Einstellungen gehen auch ohne WLAN direkt am Display, auch wenn es
nur an einem USB-Netzteil hängt: **3 Sekunden auf das Tally32-Logo oben links
drücken**, dann den PIN eingeben (Standard `0000`, ändern im Menü oder im
Webinterface). Nach 5 falschen Versuchen ist das Menü eine Minute gesperrt,
nach 60 Sekunden ohne Berührung schliesst es sich.

| Kachel | Inhalt |
|---|---|
| Demomodus | starten bzw. beenden, das Menü schliesst sich dabei |
| Anzeige | Helligkeit, Display um 180° drehen, Name in der Kopfzeile |
| Farben | sechs Farben pro Zustand, jede mit Helligkeitswert in Prozent |
| Terminbuchung | Knopf ein/aus, Zeitfenster, Wochentage, Mittagspause |
| Microsoft 365 | angemeldetes Konto, neues Konto verbinden (über den Agent) |
| WLAN | Einrichtungs-WLAN ein/aus, Netzname, Passwort, Adresse |
| Info | Version, Display, Verbindung, Konto, Einrichtungs-WLAN mit Adresse, Echtzeituhr, letzter Neustart |
| PIN ändern | neuen PIN zweimal eingeben |

### Terminbuchung am Display

Im Webinterface oder im Menü einschaltbar. Dann zeigt die linke Fläche den Knopf
"Termin buchen". Besucher geben ihre E-Mail-Adresse und einen Titel ein, wählen 15, 30
oder 60 Minuten und eine freie Zeit in der laufenden oder kommenden Woche. Der
Agent legt den Termin im Kalender an und schickt die Einladung. Im Setup
einstellbar: frühester Beginn, spätestes Ende, Mittagspause, Wochentage.
Belegt sind Zeiten mit `showAs` busy, tentative, oof und workingElsewhere.
Höchstens 4 Buchungen pro Stunde. Braucht das Recht `Calendars.ReadWrite`
(nach dem Update einmal neu anmelden, je nach Tenant mit Admin-Freigabe).

Der Buchungsdialog nutzt kantengeglättete Montserrat-Schriften (SIL OFL) mit
echten Umlauten, erzeugt mit `tools/make_vlw.py`.

### Bildschirmschoner

Der Agent meldet, ob der Rechner gesperrt ist (macOS über CoreGraphics,
Windows über WTSSessionInfoEx). Bei gesperrtem Rechner bleibt zuerst die
normale Anzeige. Nach 5 Minuten Sperre startet der Bildschirmschoner: dunkler
Grund, das Logo wandert langsam über den Bildschirm, unten springt "Tap Me"
alle 30 Sekunden eine Position weiter. Antippen zeigt die normale Anzeige (der
Tipp löst sonst nichts aus), nach 2 Minuten ohne Berührung kommt der Schoner
wieder. Entsperren beendet ihn sofort. Kein Schoner im Demomodus, während einer
Buchung oder Anmeldung und ohne Notebook.

### Demomodus

Im Menü am Display oder im Webinterface unter "Demomodus" ein- und ausschalten (bleibt über einen
Neustart erhalten, per USB auch `SET demo 1` / `SET demo 0`). Das Display zeigt
dann reihum alle Zustände (auch AUSSER HAUS), alle 6 Sekunden ein neuer, und führt
einmal pro Runde eine Buchung selbst vor. Besucher können den Knopf "Termin buchen" auch
selbst ausprobieren: Die freien Zeiten sind erfunden (mit Zeitfenster,
Mittagspause und Wochentagen aus dem Setup), gebucht wird nichts. Oben rechts
steht "DEMO". Daten vom Computer werden so lange nicht angezeigt. Ohne
Uhrzeit vom Agent läuft eine Ersatzuhr ab Montag 10:00.

Passwort oder PIN vergessen: 10 Sekunden auf das Display drücken. Danach gelten
wieder die Standardpasswörter und der PIN `0000`.

## Firmware bauen und flashen

```bash
cd firmware && pio run -e guition-p4 -t upload --upload-port /dev/cu.usbmodem31201
```

Für das 10"-Display (GUITION JC8012P4A1C) die Umgebung `guition-p4-10`.

Unter Windows ist der Port das "Serielle USB-Gerät" (z. B. `COM6`). Den Agenten
vorher anhalten, er hält den Port offen.

Für einen sauberen Neustart vorher den ganzen Flash löschen:

```bash
cd firmware && pio run -e guition-p4 -t erase --upload-port /dev/cu.usbmodem31201
```

## Agent

```bash
cd agent && uv venv .venv && uv pip install -p .venv/bin/python -r requirements.txt
```

```bash
agent/.venv/bin/python agent/tally_agent.py login
```

```bash
agent/.venv/bin/python agent/tally_agent.py
```

Weitere Befehle: `status` (einmal ausgeben, ohne Gerät), `demo` (alle Zustände
durchspielen), `logout`.

Zwei Displays an einem Rechner: je Display ein Agent, gewählt über die USB-Seriennummer
(`"port": "sn:<nummer>"` in `config.json` bzw. `agent/install_mac.sh display 2 sn:<nummer>`),
siehe [Anleitung, Abschnitt 9](docs/Anleitung.md#9-zwei-displays-an-einem-computer).

Konfiguration in `~/.config/tally32/config.json`, das Token liegt daneben in
`token_cache.json` (Rechte 600). Ohne Anmeldung läuft nur die Mikrofon Erkennung.

### Autostart auf dem Mac

```bash
agent/install_mac.sh
```

Legt den LaunchAgent `ch.aussie.tally32` an: startet bei der Anmeldung, wird nach
einem Absturz neu gestartet, Log in `~/Library/Logs/tally32.log`. Entfernen mit
`agent/install_mac.sh uninstall`. Nach `login`/`logout` den Agenten neu starten
(Skript nochmals ausführen).

### Windows

Der Agent ist dasselbe Python-Skript wie auf dem Mac. Einrichten (PowerShell im
Ordner `agent`):

```
powershell -ExecutionPolicy Bypass -File .\install_windows.ps1
```

Das legt die venv an, installiert die Pakete und registriert die Aufgabe
"Tally32 Agent" in der Aufgabenplanung (Start bei Anmeldung, unsichtbar, Neustart
nach Absturz). Log: `%LOCALAPPDATA%\tally32\tally32.log`. Entfernen mit
`-Uninstall`. Anmelden bei Microsoft 365:
`.venv\Scripts\python.exe tally_agent.py login` oder über den Knopf im Webinterface.

Testplan (Windows 11, 21.10.2026: Schritte 1 bis 5 bestanden):

1. Display mit der oberen USB-C-Buchse an die Dockingstation anstecken, im Geräte-Manager erscheint
   "Serielles USB-Gerät (COMx)" und "USB JTAG/serial debug unit". Der Treiber
   ist in Windows enthalten.
2. `.venv\Scripts\python.exe tally_agent.py status` läuft ohne Fehler.
3. `.venv\Scripts\python.exe tally_agent.py demo` findet das Display und spielt
   die Zustände ab.
4. Teams- bzw. 3CX-Anruf: Display springt auf ON AIR mit der richtigen Quelle.
5. Win+L, 5 Minuten warten: Bildschirmschoner erscheint, im Log steht
   "Bildschirm gesperrt".
6. `install_windows.ps1`, abmelden und wieder anmelden: Agent startet von selbst.
7. Notebook abdocken: Display zeigt NICHT AM PLATZ bzw. "zurück ca.".

Falls Python auf dem PC nicht erlaubt ist: den Agenten mit PyInstaller zu einer
einzelnen `tally32-agent.exe` verpacken (auf einem Rechner mit Python bauen).

### Konto wechseln (z. B. privates Konto → Firmenkonto)

Im Menü am Display unter "Microsoft 365", im Webinterface "Neue Anmeldung
starten" oder `tally_agent.py login`: das bisherige Konto wird ersetzt. Vorher
`"booking": true` in `config.json` setzen, sonst fehlt das Recht zum Buchen.
Im neuen Tenant braucht es je nach Einstellung eine Admin-Freigabe für die
Rechte `Calendars.ReadBasic`, `Presence.Read` und `Calendars.ReadWrite` (Client
"Microsoft Graph Command Line Tools" oder eine eigene App-Registrierung, siehe
unten). Ohne Freigabe laufen nur Mikrofon und Bildschirmsperre.

Freigabe ohne PowerShell: einmal mit dem Admin-Konto anmelden und bei der
Zustimmung "Consent on behalf of your organization" anhaken. Der Agent lehnt
das Admin-Konto danach ab (kein Postfach, das bisherige Konto bleibt), die
Freigabe gilt aber. Dann mit dem eigenen Konto anmelden. Konten ohne
Exchange-Postfach nimmt der Agent grundsätzlich nicht an.

### Microsoft 365

Standardmässig wird der öffentliche Client "Microsoft Graph Command Line Tools"
mit Device Code Login verwendet (Rechte `Calendars.ReadBasic`, `Presence.Read`,
mit Buchung `Calendars.ReadWrite`, Tenant `common`). Je nach Tenant muss ein Admin die Rechte einmal freigeben,
danach können sich normale Benutzer anmelden. Das angemeldete Konto braucht ein
Exchange Postfach, sonst gibt es nur den Teams Status. Blockiert der Tenant den
Client ganz, eine eigene App Registrierung anlegen (Public Client, "Allow public
client flows" an, delegierte Rechte wie oben) und deren ID als `client_id`
eintragen. Mit `"presence": false` wird nur der Kalender abgefragt.

### Grenzen

- Ein 3CX **Tischtelefon** belegt kein Mikrofon am Computer. Dafür bräuchte es
  die 3CX Call Control API.
- Die Mikrofon Erkennung pro Programm braucht macOS 14.2 oder neuer, ältere
  Versionen erkennen nur "Mikrofon belegt".
- Die Windows Variante ist auf Windows 11 getestet (Mikrofon, Sperre, Bildschirmschoner).
  Noch offen: Autostart nach neuer Anmeldung, Abdocken und Microsoft 365.

## Protokoll (115200 Baud, eine Zeile pro Meldung)

```
HELLO                                       -> TALLY32 1
ST <call|busy|away|free> <HH:MM> <frei_ab> <frei_bis> <quelle> [<free|busy|away>]
```

Das letzte Feld kommt nur bei `call`: der Zustand ohne Gespräch. `frei_ab` und
`frei_bis` gehören dann zu diesem Zustand, das Display zeigt ihn mit pulsierendem
ON AIR darüber.

Leere Felder als `-`, Leerzeichen in der Quelle als `_`. `frei_ab` ist bei
call/busy/away das Ende des laufenden Blocks, bei free das Ende des nächsten
Blocks ("danach wieder frei ab"). Liegt es nicht mehr heute, kommt `TT.MM.`.

Offline-Puffer (Agent an Display, alle 30 s):

```
TM <unix_utc> <utc_offset_s>
LK <0|1>                                   Rechner gesperrt (alle 2 s)
CB <yyyy-mm-dd> <HHMM-HHMMb|a,...|->      b = besetzt, a = abwesend
STATUS                                     -> Diagnose: angezeigter Zustand
```

Terminbuchung:

```
SLOTS <dauer> <früheste_min> <späteste_min> <tage_maske> <mittag_von> <mittag_bis>
SL <yyyy-mm-dd> <wochentag> <HHMM,...|->   ...   SL END
BOOK <yyyy-mm-dd> <HH:MM> <dauer> <email> <titel>
BK OK | BK ERR <text>
```

Einstellungen (bleiben im NVS gespeichert):

```
SET flip 1          Display um 180 Grad drehen
SET bright 180      Helligkeit 12 bis 255
SET demo 1          Demomodus ein (0 = aus)
```

## Lizenz

Tally32 Basic steht unter der **PolyForm Noncommercial License 1.0.0**
([LICENSE.md](LICENSE.md)). Erlaubt ist die Nutzung für nicht kommerzielle Zwecke:
privat sowie durch gemeinnützige Organisationen, Bildungs- und Forschungseinrichtungen
und öffentliche Stellen, auch mit Änderungen und Weitergabe zu diesen Zwecken.

**Kommerzielle Nutzung ist nicht erlaubt**, auch nicht in Teilen: weder der Einsatz in
einem Unternehmen noch der Einbau in ein Produkt oder der Betrieb als Dienst. Dafür gibt
es eine kommerzielle Lizenz. Kontakt: Oliver Timme, hello@timme-security.ch.
Bestandteile Dritter behalten ihre eigenen Lizenzen, sie stehen in [NOTICE.txt](NOTICE.txt).
