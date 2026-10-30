#!/usr/bin/env python3
"""
tally_agent.py - Arbeitsplatz-Status an die Tally32-App auf dem Display schicken.

Quellen (nach Prioritaet):
  1. Mikrofon belegt (macOS CoreAudio / Windows Datenschutz-Registry)  -> call
     Deckt Teams, 3CX-Softphone, Zoom, Browser usw. ab, ohne Konto.
  2. Teams-Presence via Microsoft Graph (InACall, Presenting, DND)     -> call / busy
  3. Outlook-Kalender via Microsoft Graph (showAs busy/oof)            -> busy
     inkl. "frei ab" (Ende des zusammenhaengenden Terminblocks)
     und "frei bis" (Beginn des naechsten Termins heute)

Aufrufe:
  tally_agent.py            Dauerbetrieb
  tally_agent.py login      Einmalig bei Microsoft 365 anmelden (Device Code)
  tally_agent.py logout     Gespeichertes Token loeschen
  tally_agent.py status     Status einmal ermitteln und ausgeben (ohne Geraet)
  tally_agent.py demo       Zustaende der Reihe nach ans Geraet schicken
  Option --log <datei>      Ausgabe in eine Datei (fuer den Autostart unter Windows)

Konfiguration: ~/.config/tally32/config.json (wird beim ersten Start angelegt).
"""
import datetime as dt
import json
import os
import queue
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

import serial
import serial.tools.list_ports

CONF_DIR = os.path.expanduser("~/.config/tally32")
CONF_FILE = os.path.join(CONF_DIR, "config.json")
TOKEN_FILE = os.path.join(CONF_DIR, "token_cache.json")

DEFAULTS = {
    "port": "auto",                 # oder z.B. "/dev/cu.usbserial-3120" bzw. "COM5"
    "graph": True,                  # Kalender + Presence ueber Microsoft Graph
    # Oeffentlicher Client "Microsoft Graph Command Line Tools". Eigene
    # App-Registrierung (Public Client, Device Code) hier eintragen, falls
    # der Tenant den nicht zulaesst.
    "client_id": "14d82eec-204b-4c2f-b7e8-296a70dab67e",
    "tenant": "common",             # Arbeits-/Schulkonten und private Microsoft-Konten
    "presence": True,               # Teams-Status (nur Arbeits-/Schulkonten)
    "busy_show_as": ["busy"],       # "tentative" ergaenzen, wenn Vorbehalt auch zaehlt
    "away_show_as": ["oof", "workingElsewhere"],  # Abwesend / an anderem Ort -> ABWESEND
    "teams_away": True,             # Teams "Abwesend"/"Gleich zurueck" -> ABWESEND
    "merge_gap_min": 5,             # Luecken bis hier gelten als durchgehend besetzt
    "mic": True,
    "mic_ignore": [],               # Bundle-IDs / Programmnamen, die nie "call" ausloesen
    # Terminbuchung am Display. Standard aus: dann fordert der Agent nur
    # Calendars.ReadBasic und Presence.Read an, mit true zusaetzlich Calendars.ReadWrite.
    "booking": False,
    "booking_lead_min": 10,         # fruehester Beginn: jetzt + Vorlauf
    "booking_subject": "{title}",   # Platzhalter {title} und {email}
    # Leserecht Kalender: ReadBasic genuegt fuer showAs, start, end, isCancelled
    # und isAllDay (gibt keinen Text, keine Anhaenge und Erweiterungen frei).
    # "Calendars.Read" nur eintragen, falls der Tenant ReadBasic nicht zulaesst.
    "calendar_scope": "Calendars.ReadBasic",
    "presence_interval_s": 10,
    "calendar_interval_s": 60,
    "send_interval_s": 2,
}

LEGACY_READ = "Calendars.Read"                   # Anmeldungen aelterer Versionen


def scopes(cfg):
    return [cfg["calendar_scope"]] + (["Presence.Read"] if cfg["presence"] else [])


WRITE_SCOPES = ["Calendars.ReadWrite"]           # fuer die Terminbuchung am Display


def login_scopes(cfg):
    return scopes(cfg) + (WRITE_SCOPES if cfg["booking"] else [])
GRAPH = "https://graph.microsoft.com/v1.0"


# ============================================================== Token-Speicher
class TokenStore:
    """Ablage des MSAL-Token-Caches. Verschluesselt ueber msal-extensions:
    Windows DPAPI (Datei token_cache.bin, nur fuer dieses Benutzerkonto lesbar),
    macOS Schluesselbund (Eintrag "Tally32", token_cache.bin ist nur die
    Aenderungsmarke). Ohne msal-extensions bzw. auf anderen Systemen die
    unverschluesselte Datei token_cache.json mit Rechten 600 wie bisher."""

    ENC_FILE = os.path.join(CONF_DIR, "token_cache.bin")

    def __init__(self):
        self.persistence = None
        try:
            from msal_extensions import persistence as px
            if sys.platform == "win32":
                self.persistence = px.FilePersistenceWithDataProtection(self.ENC_FILE)
            elif sys.platform == "darwin":
                self.persistence = px.KeychainPersistence(self.ENC_FILE, "Tally32", "token_cache")
        except Exception as e:  # noqa: BLE001 - dann wie bisher als Datei
            log("Token-Speicher: Verschluesselung nicht verfuegbar, Datei", TOKEN_FILE, f"({e})")
        self.encrypted = self.persistence is not None
        if self.encrypted:
            self._migrate()

    def kind(self):
        if not self.encrypted:
            return "Datei (unverschluesselt, Rechte 600)"
        return "DPAPI" if sys.platform == "win32" else "Schluesselbund"

    def _migrate(self):
        """Token aus der alten Datei uebernehmen und die Datei danach loeschen."""
        if not os.path.exists(TOKEN_FILE):
            return
        try:
            with open(TOKEN_FILE, encoding="utf-8") as f:
                txt = f.read()
            if self.load() is None:              # verschluesselter Speicher noch leer
                self.persistence.save(txt)
                if self.load() != txt:
                    raise RuntimeError("Kontrolle nach dem Speichern fehlgeschlagen")
            os.remove(TOKEN_FILE)
            log("Token-Speicher: Token aus", TOKEN_FILE, "in", self.kind(), "uebernommen, Datei geloescht")
        except Exception as e:  # noqa: BLE001 - alte Datei bleibt, naechster Versuch beim Start
            log("Token-Speicher: Uebernahme fehlgeschlagen:", e)

    def load(self):
        """Gespeicherter Cache als Text oder None."""
        if not self.encrypted:
            if not os.path.exists(TOKEN_FILE):
                return None
            with open(TOKEN_FILE, encoding="utf-8") as f:
                return f.read()
        from msal_extensions.persistence import PersistenceNotFound
        try:
            txt = self.persistence.load()
        except PersistenceNotFound:
            return None
        return txt if txt and txt.strip() != "{}" else None

    def save(self, txt):
        if not self.encrypted:
            fd = os.open(TOKEN_FILE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write(txt)
            return
        self.persistence.save(txt)

    def clear(self):
        if self.encrypted and sys.platform == "win32":
            if os.path.exists(self.ENC_FILE):
                os.remove(self.ENC_FILE)
        elif self.encrypted:
            self.persistence.save("{}")           # Schluesselbund-Eintrag leeren
        if os.path.exists(TOKEN_FILE):
            os.remove(TOKEN_FILE)


def load_config():
    os.makedirs(CONF_DIR, exist_ok=True)
    cfg = dict(DEFAULTS)
    if os.path.exists(CONF_FILE):
        with open(CONF_FILE, encoding="utf-8") as f:
            cfg.update(json.load(f))
    else:
        with open(CONF_FILE, "w", encoding="utf-8") as f:
            json.dump(DEFAULTS, f, indent=2)
    return cfg


LOG_FILE = None                                   # --log <datei>, z. B. unter Windows mit pythonw


def log(*a):
    line = " ".join([time.strftime("%H:%M:%S")] + [str(x) for x in a])
    if LOG_FILE:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    else:
        print(line, flush=True)


# ============================================================== Mikrofon
def app_label(ident):
    s = (ident or "").lower()
    if "teams" in s:
        return "Teams"
    if "3cx" in s:
        return "3CX"
    if "zoom" in s:
        return "Zoom"
    if "webex" in s:
        return "Webex"
    if any(b in s for b in ("chrome", "safari", "edge", "firefox", "arc")):
        return "Browser"
    return "Mikrofon"


if sys.platform == "darwin":
    import ctypes
    import ctypes.util
    from ctypes import POINTER, byref, c_int32, c_uint32, c_void_p

    _CA = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreAudio"))
    _CF = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreFoundation"))

    class _Addr(ctypes.Structure):
        _fields_ = [("sel", c_uint32), ("scope", c_uint32), ("elem", c_uint32)]

    _CA.AudioObjectGetPropertyDataSize.argtypes = [c_uint32, POINTER(_Addr), c_uint32, c_void_p, POINTER(c_uint32)]
    _CA.AudioObjectGetPropertyData.argtypes = [c_uint32, POINTER(_Addr), c_uint32, c_void_p, POINTER(c_uint32), c_void_p]
    _CF.CFStringGetCString.argtypes = [c_void_p, ctypes.c_char_p, ctypes.c_long, c_uint32]
    _CF.CFRelease.argtypes = [c_void_p]

    def _cc(s):
        return int.from_bytes(s.encode(), "big")

    _GLOB, _INPT, _SYS = _cc("glob"), _cc("inpt"), 1

    def _size(obj, sel, scope=_GLOB):
        a = _Addr(_cc(sel), scope, 0)
        n = c_uint32(0)
        if _CA.AudioObjectGetPropertyDataSize(obj, byref(a), 0, None, byref(n)) != 0:
            return None
        return n.value

    def _get(obj, sel, ctype, scope=_GLOB):
        a = _Addr(_cc(sel), scope, 0)
        v = ctype()
        n = c_uint32(ctypes.sizeof(v))
        if _CA.AudioObjectGetPropertyData(obj, byref(a), 0, None, byref(n), byref(v)) != 0:
            return None
        return v.value

    def _ids(obj, sel, scope=_GLOB):
        size = _size(obj, sel, scope)
        if not size:
            return []
        arr = (c_uint32 * (size // 4))()
        a = _Addr(_cc(sel), scope, 0)
        n = c_uint32(size)
        if _CA.AudioObjectGetPropertyData(obj, byref(a), 0, None, byref(n), arr) != 0:
            return []
        return list(arr)[: n.value // 4]

    def _bundle_id(proc):
        ref = _get(proc, "pbid", c_void_p)
        if not ref:
            return ""
        buf = ctypes.create_string_buffer(256)
        ok = _CF.CFStringGetCString(ref, buf, 256, 0x08000100)
        _CF.CFRelease(ref)
        return buf.value.decode() if ok else ""

    def mic_in_use(ignore):
        """(aktiv, Programm). Ab macOS 14.2 pro Prozess, sonst pro Geraet."""
        procs = _ids(_SYS, "prs#")
        if procs:
            for p in procs:
                if _get(p, "piri", c_uint32):
                    bid = _bundle_id(p)
                    if any(i.lower() in bid.lower() for i in ignore if i):
                        continue
                    return True, app_label(bid)
            return False, None
        for d in _ids(_SYS, "dev#"):
            if _size(d, "stm#", _INPT) and _get(d, "gone", c_uint32):
                return True, "Mikrofon"
        return False, None

elif sys.platform == "win32":
    import winreg

    _CS = r"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone"

    def _walk(root, path):
        try:
            k = winreg.OpenKey(root, path)
        except OSError:
            return
        i = 0
        while True:
            try:
                sub = winreg.EnumKey(k, i)
            except OSError:
                break
            i += 1
            yield path + "\\" + sub, sub

    def _active(root, path):
        try:
            k = winreg.OpenKey(root, path)
            stop, _ = winreg.QueryValueEx(k, "LastUsedTimeStop")
            start, _ = winreg.QueryValueEx(k, "LastUsedTimeStart")
            return stop == 0 and start > 0
        except OSError:
            return False

    def mic_in_use(ignore):
        root = winreg.HKEY_CURRENT_USER
        keys = list(_walk(root, _CS)) + list(_walk(root, _CS + r"\NonPackaged"))
        for path, name in keys:
            if name == "NonPackaged":
                continue
            if _active(root, path):
                if any(i.lower() in name.lower() for i in ignore if i):
                    continue
                return True, app_label(name)
        return False, None

else:
    def mic_in_use(ignore):
        return False, None


# ============================================================== Bildschirmsperre
if sys.platform == "darwin":
    _CG = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreGraphics"))
    _CG.CGSessionCopyCurrentDictionary.restype = c_void_p
    _CF.CFStringCreateWithCString.restype = c_void_p
    _CF.CFStringCreateWithCString.argtypes = [c_void_p, ctypes.c_char_p, c_uint32]
    _CF.CFDictionaryGetValue.restype = c_void_p
    _CF.CFDictionaryGetValue.argtypes = [c_void_p, c_void_p]
    _CF.CFBooleanGetValue.restype = ctypes.c_bool
    _CF.CFBooleanGetValue.argtypes = [c_void_p]
    _LOCK_KEY = _CF.CFStringCreateWithCString(None, b"CGSSessionScreenIsLocked", 0x08000100)

    def screen_locked():
        d = _CG.CGSessionCopyCurrentDictionary()
        if not d:
            return False
        try:
            v = _CF.CFDictionaryGetValue(d, _LOCK_KEY)
            return bool(v) and _CF.CFBooleanGetValue(v)
        finally:
            _CF.CFRelease(d)

elif sys.platform == "win32":
    import ctypes
    from ctypes import wintypes

    class _WTSINFOEX_LEVEL1(ctypes.Structure):
        _fields_ = [("SessionId", wintypes.ULONG), ("SessionState", ctypes.c_int),
                    ("SessionFlags", wintypes.LONG),          # weitere Felder werden nicht gebraucht
                    ("_align", ctypes.c_int64)]  # wie LARGE_INTEGER im Original: Data liegt bei Offset 8, nicht 4

    class _WTSINFOEX(ctypes.Structure):
        _fields_ = [("Level", wintypes.DWORD), ("Data", _WTSINFOEX_LEVEL1)]

    _WTS = ctypes.WinDLL("wtsapi32")

    def screen_locked():
        """WTSSessionInfoEx: SessionFlags 0 = gesperrt, 1 = entsperrt (ab Windows 8)."""
        buf, n = ctypes.c_void_p(), wintypes.DWORD()
        if not _WTS.WTSQuerySessionInformationW(None, wintypes.DWORD(0xFFFFFFFF), 25,
                                                ctypes.byref(buf), ctypes.byref(n)):
            return False
        try:
            info = ctypes.cast(buf, ctypes.POINTER(_WTSINFOEX)).contents
            return info.Level == 1 and info.Data.SessionFlags == 0
        finally:
            _WTS.WTSFreeMemory(buf)

else:
    def screen_locked():
        return False


# ============================================================== Microsoft Graph
class Graph:
    def __init__(self, cfg, fresh=False, store=None):
        import msal
        self.store = store or TokenStore()
        self.cache = msal.SerializableTokenCache()    # im Speicher, nur ein Konto
        if not fresh:
            try:
                txt = self.store.load()
                if txt:
                    self.cache.deserialize(txt)
            except Exception as e:  # noqa: BLE001 - z.B. Schluesselbund gesperrt
                log("Token-Speicher: Lesen fehlgeschlagen:", e)
        self.scopes = scopes(cfg)
        self.login_scopes = login_scopes(cfg)
        self.app = msal.PublicClientApplication(
            cfg["client_id"],
            authority=f"https://login.microsoftonline.com/{cfg['tenant']}",
            token_cache=self.cache,
        )

    def _save(self):
        if self.cache.has_state_changed:
            try:
                self.store.save(self.cache.serialize())
            except Exception as e:  # noqa: BLE001 - Token bleibt im Speicher gueltig
                log("Token-Speicher: Schreiben fehlgeschlagen:", e)

    def login(self):
        flow = self.app.initiate_device_flow(scopes=self.login_scopes)
        if "user_code" not in flow:
            raise RuntimeError(flow.get("error_description", "Device Flow fehlgeschlagen"))
        print(flow["message"], flush=True)
        res = self.app.acquire_token_by_device_flow(flow)
        if "access_token" not in res:
            raise RuntimeError(res.get("error_description", "Anmeldung fehlgeschlagen"))
        self.check_mailbox(res)                     # vor dem Speichern: altes Konto bleibt sonst
        self._save()
        print("Angemeldet als", res.get("id_token_claims", {}).get("preferred_username", "?"))

    def check_mailbox(self, res):
        """Konto ohne Exchange-Postfach ablehnen (z. B. ein Admin-Konto, mit dem bei der
        Zustimmung versehentlich die ganze Anmeldung abgeschlossen wurde). Prueft mit dem
        frischen Token eine Minute Kalender, ohne den Cache zu speichern. Nur die
        eindeutige Antwort MailboxNotEnabledForRESTAPI lehnt ab, andere Fehler (Netz,
        fehlende Zustimmung) entscheiden hier nicht."""
        start = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
        q = urllib.parse.urlencode({
            "startDateTime": start.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "endDateTime": (start + dt.timedelta(minutes=1)).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "$top": 1, "$select": "showAs"})
        req = urllib.request.Request(GRAPH + "/me/calendarView?" + q,
                                     headers={"Authorization": "Bearer " + res["access_token"]})
        try:
            with urllib.request.urlopen(req, timeout=20):
                return
        except urllib.error.HTTPError as e:
            if "MailboxNotEnabledForRESTAPI" not in e.read().decode("utf-8", "replace"):
                return
        except (urllib.error.URLError, TimeoutError, OSError):
            return
        user = res.get("id_token_claims", {}).get("preferred_username", "?")
        log("Anmeldung abgelehnt:", user, "hat kein Exchange-Postfach. Bisheriges Konto bleibt.")
        raise RuntimeError("Dieses Konto hat kein Postfach. Mit dem eigenen Konto anmelden.")

    def username(self):
        accounts = self.app.get_accounts()
        return accounts[0].get("username", "") if accounts else ""

    def token(self, write=False):
        accounts = self.app.get_accounts()
        if not accounts:
            return None
        res = self.app.acquire_token_silent(WRITE_SCOPES if write else self.scopes, account=accounts[0])
        if not (res and "access_token" in res) and not write and LEGACY_READ not in self.scopes:
            # Anmeldung einer aelteren Version mit Calendars.Read: weiter nutzen,
            # bis zur naechsten Anmeldung (dann nur noch das kleinere Recht)
            legacy = [LEGACY_READ] + self.scopes[1:]
            res = self.app.acquire_token_silent(legacy, account=accounts[0])
            if res and "access_token" in res:
                log("Graph: Token mit", LEGACY_READ, "aus frueherer Anmeldung, neu anmelden fuer", self.scopes[0])
                self.scopes = legacy
        self._save()
        return res.get("access_token") if res else None

    def request(self, method, path, params=None, data=None, write=False):
        tok = self.token(write)
        if not tok:
            if write:
                raise PermissionError("Schreibrecht fehlt, im Setup neu bei Office 365 anmelden")
            raise PermissionError("nicht angemeldet (tally_agent.py login)")
        url = path if path.startswith("https://") else GRAPH + path
        if params:
            url += "?" + urllib.parse.urlencode(params)
        headers = {"Authorization": "Bearer " + tok, "Prefer": 'outlook.timezone="UTC"'}
        body = None
        if data is not None:
            body = json.dumps(data).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=body, method=method, headers=headers)
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.load(r)

    def get(self, path, params=None):
        return self.request("GET", path, params)

    def events_range(self, start, end):
        """Alle Termine zwischen start und end (Serien aufgeloest, mit Paging)."""
        out = []
        data = self.get("/me/calendarView", {
            "startDateTime": start.astimezone(dt.timezone.utc).isoformat(),
            "endDateTime": end.astimezone(dt.timezone.utc).isoformat(),
            "$select": "showAs,start,end,isCancelled",
            "$top": "200",
        })
        while True:
            out.extend(data.get("value", []))
            nxt = data.get("@odata.nextLink")
            if not nxt:
                return out
            data = self.get(nxt)

    def create_event(self, start, end, email, subject):
        """Erinnerung sofort faellig: Outlook und Teams melden kurzfristige Buchungen gleich."""
        fmt = "%Y-%m-%dT%H:%M:%S"
        lead = max(0, int((start - dt.datetime.now().astimezone()).total_seconds() // 60))
        return self.request("POST", "/me/events", write=True, data={
            "subject": subject,
            "start": {"dateTime": start.astimezone(dt.timezone.utc).strftime(fmt), "timeZone": "UTC"},
            "end": {"dateTime": end.astimezone(dt.timezone.utc).strftime(fmt), "timeZone": "UTC"},
            "attendees": [{"emailAddress": {"address": email}, "type": "required"}],
            "showAs": "busy",
            "isReminderOn": True,
            "reminderMinutesBeforeStart": lead,
            "body": {"contentType": "text", "content": "Spontan am Tally32-Display gebucht."},
        })

    def presence(self):
        return self.get("/me/presence")

    def events_today(self):
        now = dt.datetime.now().astimezone()
        start = now.replace(hour=0, minute=0, second=0, microsecond=0)
        end = start + dt.timedelta(days=2)       # morgen dazu, damit Bloecke ueber Mitternacht stimmen
        out, path = [], "/me/calendarView"
        params = {
            "startDateTime": start.astimezone(dt.timezone.utc).isoformat(),
            "endDateTime": end.astimezone(dt.timezone.utc).isoformat(),
            "$select": "showAs,start,end,isCancelled,isAllDay",
            "$top": "100",
        }
        data = self.get(path, params)
        out.extend(data.get("value", []))
        return out


def _utc(s):
    s = s.split(".")[0]
    return dt.datetime.fromisoformat(s).replace(tzinfo=dt.timezone.utc).astimezone()


def day_blocks(events, cfg, day):
    """Zusammenhaengende Terminbloecke eines Tages fuer den Offline-Puffer des
    Displays: Liste (start, ende, abwesend), auf den Tag zugeschnitten."""
    away_set = set(cfg["away_show_as"])
    blocking = set(cfg["busy_show_as"]) | away_set
    d0, d1 = day, day + dt.timedelta(days=1)
    items = []
    for e in events:
        if e.get("isCancelled") or e.get("showAs") not in blocking:
            continue
        s, t = _utc(e["start"]["dateTime"]), _utc(e["end"]["dateTime"])
        if t <= d0 or s >= d1:
            continue
        items.append((max(s, d0), min(t, d1), e["showAs"] in away_set))
    items.sort()
    gap = dt.timedelta(minutes=cfg["merge_gap_min"])
    out = []
    for s, t, a in items:
        if out and s <= out[-1][1] + gap:
            ps, pt, pa = out[-1]
            out[-1] = (ps, max(pt, t), pa or a)
        else:
            out.append((s, t, a))
    return out


def cache_lines(events, cfg, now):
    """TM <unix_utc> <utc_offset_s> und CB <yyyy-mm-dd> <HHMM-HHMMb|a,...|-> fuer heute und morgen."""
    lines = [f"TM {int(now.timestamp())} {int(now.utcoffset().total_seconds())}"]
    today = now.replace(hour=0, minute=0, second=0, microsecond=0)
    for i in range(2):
        day = today + dt.timedelta(days=i)
        end = day + dt.timedelta(days=1)

        def hm(t):
            return "2400" if t >= end else t.strftime("%H%M")
        txt = ",".join(f"{hm(s)}-{hm(t)}{'a' if a else 'b'}" for s, t, a in day_blocks(events, cfg, day)) or "-"
        lines.append(f"CB {day.strftime('%Y-%m-%d')} {txt}")
    return lines


def calendar_times(events, cfg, now):
    """Terminbloecke auswerten. Rueckgabe dict mit datetime oder None:
    block_end  Ende des laufenden Blocks (frei ab)
    block_away laufender Block ist Abwesenheit (oof / an anderem Ort)
    next_start Beginn des naechsten Blocks heute (frei bis)
    next_end   Ende dieses naechsten Blocks (danach wieder frei ab)
    Termine mit Luecken bis merge_gap_min zaehlen als ein Block."""
    away_set = set(cfg["away_show_as"])
    blocking = set(cfg["busy_show_as"]) | away_set
    items = []
    for e in events:
        if e.get("isCancelled") or e.get("showAs") not in blocking:
            continue
        items.append((_utc(e["start"]["dateTime"]), _utc(e["end"]["dateTime"]), e["showAs"] in away_set))
    items.sort()
    gap = dt.timedelta(minutes=cfg["merge_gap_min"])

    def block_from(start):
        end = start
        for s, e, _ in items:
            if s <= end + gap and e > end:
                end = e
        return end

    res = {"block_end": None, "block_away": False, "next_start": None, "next_end": None}
    current = [(s, e, a) for s, e, a in items if s <= now < e]
    if current:
        res["block_end"] = block_from(max(e for _, e, _ in current))
        res["block_away"] = any(a for _, _, a in current)
        return res
    upcoming = [s for s, _, _ in items if s > now and s.date() == now.date()]
    if upcoming:
        res["next_start"] = min(upcoming)
        res["next_end"] = block_from(res["next_start"])
    return res


def fmt_time(t, now):
    if t is None:
        return "-"
    if t.date() == now.date():
        return t.strftime("%H:%M")
    return t.strftime("%d.%m.")                  # z.B. Abwesenheit bis uebermorgen


# ============================================================== Status
class StatusSource:
    def __init__(self, cfg):
        self.cfg = cfg
        self.graph = None
        self.presence = {}
        self.events = []
        self.t_presence = 0
        self.t_calendar = 0
        self.use_presence = cfg["presence"]
        self.warned_mailbox = False
        self.events_ok = False                     # Kalender mindestens einmal gelesen
        if cfg["graph"]:
            try:
                self.graph = Graph(cfg)
                log("Token-Speicher:", self.graph.store.kind())
                if not self.graph.app.get_accounts():
                    log("Graph: nicht angemeldet, nur Mikrofon aktiv (tally_agent.py login)")
            except ImportError:
                log("Graph: Paket msal fehlt, nur Mikrofon aktiv")
                self.graph = None

    def use_graph(self, g):
        """Nach neuer Anmeldung: neues Konto verwenden, Daten neu holen."""
        self.graph = g
        self.presence, self.events = {}, []
        self.t_presence = self.t_calendar = 0
        self.use_presence = self.cfg["presence"]
        self.warned_mailbox = False

    def username(self):
        return self.graph.username() if self.graph else ""

    def can_write(self):
        if not self.cfg["booking"]:
            return False                           # Buchung am Display aus
        try:
            return bool(self.graph and self.graph.token(write=True))
        except Exception:  # noqa: BLE001
            return False

    def _refresh_graph(self):
        if not self.graph or not self.graph.app.get_accounts():
            return
        now = time.time()
        if self.use_presence and now - self.t_presence >= self.cfg["presence_interval_s"]:
            self.t_presence = now
            try:
                self.presence = self.graph.presence()
            except urllib.error.HTTPError as e:
                if 400 <= e.code < 500 and e.code != 429:
                    log(f"Teams-Status nicht verfuegbar (HTTP {e.code}), z.B. privates Konto. Nur Kalender.")
                    self.use_presence = False
                else:
                    log("Graph-Fehler Presence:", e)
            except (urllib.error.URLError, PermissionError, TimeoutError, OSError) as e:
                log("Graph-Fehler Presence:", e)
        if now - self.t_calendar >= self.cfg["calendar_interval_s"]:
            self.t_calendar = now
            try:
                self.events = self.graph.events_today()
                self.events_ok = True
            except urllib.error.HTTPError as e:
                body = e.read().decode("utf-8", "replace")
                if "MailboxNotEnabledForRESTAPI" in body:
                    if not self.warned_mailbox:
                        log("Kalender: angemeldetes Konto hat kein Exchange-Postfach. Mit dem Postfach-Konto neu anmelden (logout, login).")
                    self.warned_mailbox = True
                else:
                    log(f"Graph-Fehler Kalender: HTTP {e.code} {body[:200]}")
            except (urllib.error.URLError, PermissionError, TimeoutError, OSError) as e:
                log("Graph-Fehler Kalender:", e)

    def current(self):
        self._refresh_graph()
        now = dt.datetime.now().astimezone()
        mic, app = mic_in_use(self.cfg["mic_ignore"]) if self.cfg["mic"] else (False, None)
        activity = self.presence.get("activity", "")
        availability = self.presence.get("availability", "")
        cal = calendar_times(self.events, self.cfg, now)
        free_at, next_at = cal["block_end"], None

        # Erst der normale Zustand, ein Gespraech legt sich darueber: Das Display zeigt
        # dann ON AIR plus diesen Zustand mit seinen Zeiten (base, z. B. "frei bis 13:00").
        call = "call" if mic else ("Teams" if activity in ("InACall", "InAConferenceCall", "Presenting") else None)
        if call == "call":
            call = app
        if free_at and cal["block_away"]:
            mode, src = "away", "laut Kalender"
        elif free_at:
            mode, src = "busy", "Termin im Kalender"
        elif availability == "DoNotDisturb" or activity == "DoNotDisturb":
            mode, src = "busy", "Bitte nicht st\u00f6ren"
        elif self.cfg["teams_away"] and availability in ("Away", "BeRightBack"):
            mode, src = "away", "Teams"
        else:
            # frei: "frei bis" = naechster Termin, "frei ab" = danach wieder frei
            mode, src = "free", ""
            next_at, free_at = cal["next_start"], cal["next_end"]
        base = ""
        if call is not None:
            base, mode, src = mode, "call", call
        return {
            "base": base,
            "mode": mode,
            "clock": now.strftime("%H:%M"),
            "free_at": fmt_time(free_at, now),
            "next_at": fmt_time(next_at, now),
            "src": src,
        }


def line_for(st):
    src = st["src"].replace(" ", "_") or "-"
    base = f" {st['base']}" if st.get("base") else ""   # beim Gespraech: Zustand darunter
    return f"ST {st['mode']} {st['clock']} {st['free_at']} {st['next_at']} {src}{base}\n"


# ============================================================== Geraet
def open_port(name):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = name, 115200, 1
    s.dtr = False                                 # vor open setzen: kein Auto-Reset
    s.rts = False
    s.open()
    return s


def probe(name):
    """Port oeffnen und pruefen, ob die Tally32-App antwortet."""
    try:
        s = open_port(name)
    except (serial.SerialException, OSError):
        return None
    deadline = time.time() + 6                    # falls Open doch resettet: Boot abwarten
    while time.time() < deadline:
        s.write(b"HELLO\n")
        t = time.time() + 1
        while time.time() < t:
            line = s.readline().decode("utf-8", "replace").strip()
            if line.startswith("TALLY32"):
                return s
    s.close()
    return None


def find_device(cfg):
    """port: "auto" (erstes Tally32), ein Geraetename oder "sn:<USB-Seriennummer>" (bei
    mehreren Displays an einem Rechner, je Display ein Agent, z. B. sn:12:34:56:78:9A:BC)."""
    want = cfg["port"]
    if want != "auto" and not want.startswith("sn:"):
        return probe(want)
    for p in serial.tools.list_ports.comports():
        if p.vid is None:
            continue
        if want.startswith("sn:") and (p.serial_number or "").upper() != want[3:].upper():
            continue
        s = probe(p.device)
        if s:
            log("Geraet gefunden:", p.device)
            return s
    return None


# ============================================================== Terminbuchung
BOOK_BLOCKING = {"busy", "oof", "tentative", "workingElsewhere"}


def booking_days(now, mask):
    """Tage der laufenden und der kommenden Woche (mask: Bit 0 = Montag)."""
    monday = (now - dt.timedelta(days=now.weekday())).replace(hour=0, minute=0, second=0, microsecond=0)
    return [monday + dt.timedelta(days=i) for i in range(14) if mask & (1 << (i % 7))]


def lunch_interval(day, lunch_from, lunch_to):
    if lunch_to <= lunch_from:
        return []
    return [(day + dt.timedelta(minutes=lunch_from), day + dt.timedelta(minutes=lunch_to))]


def free_slots(busy, day, dur, earliest, latest, not_before):
    """Freie Startzeiten an einem Tag. busy: Liste (start, ende).
    Raster 15 min bei 15-min-Terminen, sonst 30 min."""
    step = dt.timedelta(minutes=15 if dur <= 15 else 30)
    d = dt.timedelta(minutes=dur)
    t = day + dt.timedelta(minutes=earliest)
    stop = day + dt.timedelta(minutes=latest)
    out = []
    while t + d <= stop:
        if t >= not_before and not any(s < t + d and e > t for s, e in busy):
            out.append(t)
        t += step
    return out


def busy_intervals(events):
    return [(_utc(e["start"]["dateTime"]), _utc(e["end"]["dateTime"]))
            for e in events if not e.get("isCancelled") and e.get("showAs") in BOOK_BLOCKING]


def local_day(day_str):
    d = dt.date.fromisoformat(day_str)
    return dt.datetime(d.year, d.month, d.day).astimezone()


def not_before(cfg, now):
    t = now + dt.timedelta(minutes=cfg["booking_lead_min"])
    q = (15 - t.minute % 15) % 15                  # auf die naechste Viertelstunde
    return (t + dt.timedelta(minutes=q)).replace(second=0, microsecond=0)


class BookingWorker:
    """SLOTS <dauer> <frueheste_min> <spaeteste_min> <tage_maske> [<mittag_von> <mittag_bis>]
         -> SL <yyyy-mm-dd> <wochentag 0..6> <HHMM,HHMM,...|->  ...  SL END
       BOOK <yyyy-mm-dd> <HH:MM> <dauer> <email> <titel ...>
         -> BK OK | BK ERR <text>"""

    def __init__(self, cfg, src, out):
        self.cfg, self.src, self.out = cfg, src, out
        self.lock = threading.Lock()

    def handle(self, line):
        threading.Thread(target=self._run, args=(line,), daemon=True).start()

    def _run(self, line):
        with self.lock:
            parts = line.split(" ", 5) if line.startswith("BOOK ") else line.split()
            try:
                if not self.cfg["booking"]:
                    raise RuntimeError("Buchung im Agenten ausgeschaltet")
                if not self.src.graph or not self.src.graph.app.get_accounts():
                    raise RuntimeError("Kein Microsoft-Konto angemeldet")
                if parts[0] == "SLOTS":
                    lunch = (int(parts[5]), int(parts[6])) if len(parts) >= 7 else (0, 0)
                    self._slots(int(parts[1]), int(parts[2]), int(parts[3]), int(parts[4]), lunch)
                elif parts[0] == "BOOK":
                    title = parts[5].strip() if len(parts) > 5 else ""
                    self._book(parts[1], parts[2], int(parts[3]), parts[4], title)
            except Exception as e:  # noqa: BLE001 - Fehler soll aufs Display
                log("Buchung:", e)
                tag = "SL" if parts[0] == "SLOTS" else "BK"
                self.out.put(f"{tag} ERR {str(e)[:80].replace(' ', '_')}")

    def _slots(self, dur, earliest, latest, mask, lunch=(0, 0)):
        now = dt.datetime.now().astimezone()
        days = booking_days(now, mask)
        if not days:
            raise RuntimeError("Keine Wochentage freigegeben")
        busy = busy_intervals(self.src.graph.events_range(days[0], days[-1] + dt.timedelta(days=1)))
        nb = not_before(self.cfg, now)
        for day in days:
            slots = free_slots(busy + lunch_interval(day, *lunch), day, dur, earliest, latest, nb)
            txt = ",".join(t.strftime("%H%M") for t in slots) or "-"
            self.out.put(f"SL {day.strftime('%Y-%m-%d')} {day.weekday()} {txt}")
        self.out.put("SL END")

    def _book(self, day_str, hhmm, dur, email, title):
        if dur not in (15, 30, 60) or "@" not in email or "." not in email.split("@")[-1]:
            raise RuntimeError("Ungueltige Angaben")
        title = title[:100] or f"Termin mit {email}"
        day = local_day(day_str)
        h, m = (int(x) for x in hhmm.split(":"))
        start = day + dt.timedelta(hours=h, minutes=m)
        end = start + dt.timedelta(minutes=dur)
        now = dt.datetime.now().astimezone()
        if start < not_before(self.cfg, now) - dt.timedelta(minutes=self.cfg["booking_lead_min"]):
            raise RuntimeError("Zeit liegt in der Vergangenheit")
        busy = busy_intervals(self.src.graph.events_range(start, end))   # Slot noch frei?
        if any(s < end and e > start for s, e in busy):
            raise RuntimeError("Zeit ist inzwischen belegt")
        subject = self.cfg["booking_subject"].format(email=email, title=title)
        self.src.graph.create_event(start, end, email, subject)
        self.src.t_calendar = 0                       # Kalender sofort neu laden
        log(f"Termin gebucht: {day_str} {hhmm} {dur} min mit {email}: {title}")
        self.out.put("BK OK")


class LoginWorker:
    """Office-365-Anmeldung, ausgeloest vom Geraet ("LOGIN" ueber USB).
    Laeuft im Hintergrund, Meldungen gehen ueber die Warteschlange ans Geraet:
    LC <code> <url>, danach LR OK <konto> bzw. LR ERR <text>."""

    def __init__(self, cfg, src, out):
        self.cfg, self.src, self.out = cfg, src, out
        self.thread = None

    def start(self):
        if self.thread and self.thread.is_alive():
            return                                  # laeuft schon, Anfrage wiederholt
        self.thread = threading.Thread(target=self._work, daemon=True)
        self.thread.start()

    def _work(self):
        try:
            g = Graph(self.cfg, fresh=True)          # altes Konto bleibt bis zum Erfolg aktiv
            flow = g.app.initiate_device_flow(scopes=g.login_scopes)
            if "user_code" not in flow:
                raise RuntimeError(flow.get("error_description", "Device Flow fehlgeschlagen"))
            log("Anmeldung gestartet, Code", flow["user_code"])
            self.out.put(f"LC {flow['user_code']} {flow['verification_uri']}")
            res = g.app.acquire_token_by_device_flow(flow)
            if "access_token" not in res:
                raise RuntimeError(res.get("error_description", "Anmeldung fehlgeschlagen").splitlines()[0])
            g.check_mailbox(res)                     # ohne Postfach: ablehnen, altes Konto bleibt
            g._save()
            self.src.use_graph(g)
            user = g.username()
            log("Angemeldet als", user)
            self.out.put(f"LR OK {user}")
            self.out.put(f"AC {user}")
            self.out.put(f"CAP write {1 if self.src.can_write() else 0}")
        except Exception as e:  # noqa: BLE001 - Fehler soll aufs Display
            log("Anmeldung fehlgeschlagen:", e)
            msg = str(e)[:90].replace(" ", "_") or "Fehler"
            self.out.put(f"LR ERR {msg}")


def run(cfg):
    src = StatusSource(cfg)
    out = queue.Queue()
    login = LoginWorker(cfg, src, out)
    booking = BookingWorker(cfg, src, out)
    dev, last, rx = None, None, b""
    locked = False
    t_send = t_account = 0
    while True:
        if dev is None:
            dev = find_device(cfg)
            if dev is None:
                log("Tally32-Geraet nicht gefunden, neuer Versuch in 10 s")
                time.sleep(10)
                continue
            dev.timeout = 0
            last, rx, t_account = None, b"", 0
        try:
            now = time.time()
            if now - t_send >= cfg["send_interval_s"]:
                t_send = now
                line = line_for(src.current())
                if line != last:
                    log(line.strip())
                    last = line
                dev.write(line.encode("utf-8"))
                lk = screen_locked()
                if lk != locked:
                    log("Bildschirm gesperrt" if lk else "Bildschirm entsperrt")
                    locked = lk
                dev.write(f"LK {1 if lk else 0}\n".encode())
            if now - t_account >= 30:
                t_account = now
                if src.events_ok:                     # Offline-Puffer fuers Display
                    for ln in cache_lines(src.events, cfg, dt.datetime.now().astimezone()):
                        out.put(ln)
                out.put(f"AC {src.username() or '-'}")
                out.put(f"CAP write {1 if src.can_write() else 0}")
            while not out.empty():
                dev.write((out.get() + "\n").encode("utf-8"))
            # Meldungen vom Geraet
            rx += dev.read(dev.in_waiting or 0)
            while b"\n" in rx:
                ln, rx = rx.split(b"\n", 1)
                ln = ln.decode("utf-8", "replace").strip()
                if ln == "LOGIN":
                    login.start()
                elif ln.startswith(("SLOTS ", "BOOK ")):
                    booking.handle(ln)
                elif ln.startswith("LOG "):
                    log("Geraet:", ln[4:])
        except (serial.SerialException, OSError) as e:
            log("USB getrennt:", e)
            try:
                dev.close()
            except Exception:
                pass
            dev = None
            continue
        time.sleep(0.2)


def demo(cfg):
    dev = find_device(cfg)
    if not dev:
        sys.exit("Tally32-Geraet nicht gefunden.")
    clk = time.strftime("%H:%M")
    for ln in (f"ST free {clk} 16:00 15:00 -", f"ST free {clk} - - -", f"ST busy {clk} 14:30 - Termin_im_Kalender",
               f"ST away {clk} 23.10. - laut_Kalender", f"ST away {clk} - - Teams",
               f"ST call {clk} 14:30 - Teams", f"ST call {clk} - - 3CX", f"ST free {clk} - - -"):
        print(ln)
        dev.write((ln + "\n").encode())
        time.sleep(4)


def main():
    global LOG_FILE
    args = sys.argv[1:]
    if "--log" in args:                           # Ausgabe in Datei statt Konsole
        i = args.index("--log")
        LOG_FILE = args[i + 1]
        del args[i:i + 2]
    cfg = load_config()
    if "--port" in args:                          # Display waehlen (ueberschreibt config.json)
        i = args.index("--port")
        cfg["port"] = args[i + 1]
        del args[i:i + 2]
    cmd = args[0] if args else "run"
    if cmd == "login":
        Graph(cfg, fresh=True).login()            # ersetzt das bisherige Konto
    elif cmd == "logout":
        TokenStore().clear()
        print("Token geloescht.")
    elif cmd == "status":
        st = StatusSource(cfg).current()
        print(json.dumps(st, ensure_ascii=False, indent=2))
    elif cmd == "demo":
        demo(cfg)
    else:
        run(cfg)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
