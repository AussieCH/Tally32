/******************************************************************************
 * web.cpp  -  Lokaler Access Point + Webinterface zur Konfiguration
 *
 *   WLAN  "Tally32-XXXX" (WPA2), Webinterface http://192.168.4.1
 *   Login admin / tally32, beim ersten Login muss das Passwort geaendert werden.
 *   Funktionen: Office-365-Anmeldung ausloesen, Farben, Terminbuchung am
 *   Display (ein/aus, Zeitfenster, Wochentage), Helligkeit/Drehung,
 *   WLAN-Passwort, Einrichtungs-WLAN abschalten, Web-Passwort.
 *
 * Der Access Point laesst sich abschalten (apOff), dann gehen alle Einstellungen
 * nur noch im Menue am Geraet. Wieder einschalten: 5 s aufs Display druecken
 * (main.cpp touchLoop).
 *
 * Eine Sitzung (Cookie), 30 min Inaktivitaet. DNS faengt alle Namen ab, damit
 * Handys die Seite als Captive Portal von selbst oeffnen.
 ******************************************************************************/
#include <WiFi.h>
#include <esp_netif.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_random.h>
#include <esp_mac.h>
#include "state.h"

static WebServer server(80);
static DNSServer dns;
static char ssid[24];
static String session;
static uint32_t sessionSeen = 0;
static bool apRunning = false;
static bool apFailed = false;                   // softAP() hat abgelehnt (Diagnose)
static const uint32_t SESSION_MS = 30UL * 60 * 1000;

const char* webSsid() { return ssid; }
int webApState() { return apRunning ? 1 : (apFailed ? 2 : 0); }

// ------------------------------------------------------------ Hilfen
static String esc(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '&': o += "&amp;"; break;
      case '"': o += "&quot;"; break;
      case '_': o += ' '; break;                // Agent ersetzt Leerzeichen durch _
      default:  o += *s;
    }
  }
  return o;
}

static String hexColor(uint32_t c) {
  char b[8]; snprintf(b, sizeof(b), "#%06lx", (unsigned long)(c & 0xFFFFFF));
  return String(b);
}

static bool parseColor(const String& s, uint32_t& out) {
  if (s.length() != 7 || s[0] != '#') return false;
  char* end = nullptr;
  unsigned long v = strtoul(s.c_str() + 1, &end, 16);
  if (*end) return false;
  out = v;
  return true;
}

static void redirect(const char* to) {
  server.sendHeader("Location", to);
  server.send(303, "text/plain", "");
}

static const char* modeName(Mode m) {
  switch (m) {
    case M_FREE: return "FREI";
    case M_BUSY: return "BESETZT";
    case M_AWAY: return "ABWESEND";
    case M_CALL: return "IM GESPR&Auml;CH";
    case M_OUT:  return "NICHT AM PLATZ";
    case M_CLOSED: return "AUSSER HAUS";
    default:     return "KEIN SIGNAL";
  }
}

// ------------------------------------------------------------ Seitenrahmen
static const char CSS[] PROGMEM = R"CSS(
:root{--bg:#f4f2ec;--card:#fff;--fg:#1d1d1f;--dim:#5f5f66;--line:#d9d6cc;--acc:#2d5bd7;--err:#a1121a;--ok:#1d6b34}
@media (prefers-color-scheme:dark){:root{--bg:#16171a;--card:#212226;--fg:#ececf0;--dim:#a2a2ac;--line:#35363c;--acc:#7da2ff;--err:#ff8a8f;--ok:#7fd69a}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 -apple-system,system-ui,Segoe UI,Roboto,sans-serif}
main{max-width:620px;margin:0 auto;padding:16px}h1{font-size:24px;margin:8px 0 16px}h2{font-size:18px;margin:0 0 12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin:0 0 16px}
label{display:block;margin:10px 0 4px;color:var(--dim);font-size:14px}
input[type=text],input[type=password],input[type=time]{width:100%;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg);font-size:16px}
button,.btn{display:inline-block;margin-top:12px;padding:10px 16px;border:0;border-radius:8px;background:var(--acc);color:#fff;font-size:16px;cursor:pointer;text-decoration:none}
button.sec{background:transparent;color:var(--acc);border:1px solid var(--acc)}
.row{display:flex;align-items:center;gap:12px;padding:8px 0;border-bottom:1px solid var(--line)}.row:last-of-type{border-bottom:0}
.row .n{flex:1}.row input[type=color]{width:56px;height:40px;border:1px solid var(--line);border-radius:8px;padding:2px;background:none}
.row code,.dim{color:var(--dim);font-size:14px}.err{color:var(--err);font-weight:600}.ok{color:var(--ok);font-weight:600}
.code{font:700 32px/1.2 ui-monospace,Menlo,monospace;letter-spacing:2px}
input[type=range]{width:100%}
)CSS";

static void sendPage(const String& title, const String& body) {
  String h;
  h.reserve(body.length() + 3000);
  h += F("<!doctype html><html lang=de><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'><title>");
  h += title;
  h += F("</title><style>");
  h += FPSTR(CSS);
  h += F("</style></head><body><main>");
  h += body;
  h += F("</main></body></html>");
  server.send(200, "text/html; charset=utf-8", h);
}

// ------------------------------------------------------------ Sitzung
static String cookieToken() {
  String c = server.header("Cookie");
  int i = c.indexOf("T32S=");
  if (i < 0) return "";
  int e = c.indexOf(';', i);
  return c.substring(i + 5, e < 0 ? c.length() : e);
}

static bool loggedIn() {
  if (session.isEmpty() || millis() - sessionSeen > SESSION_MS) return false;
  if (cookieToken() != session) return false;
  sessionSeen = millis();
  return true;
}

// true = darf weiter. Sonst Weiterleitung auf Login bzw. Passwortwechsel.
static bool guard(bool allowMustChange = false) {
  if (!loggedIn()) { redirect("/login"); return false; }
  if (!allowMustChange && authMustChange()) { redirect("/password"); return false; }
  return true;
}

// ------------------------------------------------------------ Login / Passwort
static void pageLogin(const char* msg) {
  String b = F("<h1>Tally32</h1><div class=card><h2>Anmelden</h2>");
  if (msg) { b += F("<p class=err>"); b += msg; b += F("</p>"); }
  b += F("<form method=post action=/login>"
         "<label for=u>Benutzer</label><input type=text id=u name=u autocomplete=username autocapitalize=off>"
         "<label for=p>Passwort</label><input type=password id=p name=p autocomplete=current-password>"
         "<button>Anmelden</button></form></div>");
  sendPage("Tally32 Anmeldung", b);
}

static void handleLoginPost() {
  if (server.arg("u") == WEB_USER && authCheck(server.arg("p"))) {
    char t[33];
    for (int i = 0; i < 16; i++) sprintf(t + 2 * i, "%02x", (unsigned)(esp_random() & 0xFF));
    session = t;
    sessionSeen = millis();
    server.sendHeader("Set-Cookie", "T32S=" + session + "; Path=/; HttpOnly; SameSite=Strict");
    redirect(authMustChange() ? "/password" : "/");
    return;
  }
  delay(1500);                                   // Raten bremsen
  pageLogin("Benutzer oder Passwort falsch.");
}

static void pagePassword(const char* msg, bool ok) {
  bool must = authMustChange();
  String b = F("<h1>Tally32</h1><div class=card><h2>Passwort &auml;ndern</h2>");
  if (must) b += F("<p>Beim ersten Login muss das Standardpasswort ge&auml;ndert werden.</p>");
  if (msg) { b += ok ? F("<p class=ok>") : F("<p class=err>"); b += msg; b += F("</p>"); }
  b += F("<form method=post action=/password>");
  if (!must) b += F("<label for=o>Aktuelles Passwort</label><input type=password id=o name=o autocomplete=current-password>");
  b += F("<label for=n>Neues Passwort (mindestens 8 Zeichen)</label><input type=password id=n name=n autocomplete=new-password>"
         "<label for=c>Neues Passwort wiederholen</label><input type=password id=c name=c autocomplete=new-password>"
         "<button>Speichern</button></form>");
  if (!must) b += F("<p><a href=/>Zur&uuml;ck</a></p>");
  b += F("</div>");
  sendPage("Tally32 Passwort", b);
}

static void handlePasswordPost() {
  if (!guard(true)) return;
  bool must = authMustChange();
  String n = server.arg("n"), c = server.arg("c");
  if (!must && !authCheck(server.arg("o"))) { delay(1000); pagePassword("Aktuelles Passwort falsch.", false); return; }
  if (n.length() < 8)                       { pagePassword("Das neue Passwort braucht mindestens 8 Zeichen.", false); return; }
  if (n == WEB_DEFAULT_PW)                  { pagePassword("Bitte nicht das Standardpasswort verwenden.", false); return; }
  if (n != c)                               { pagePassword("Die beiden Eingaben stimmen nicht &uuml;berein.", false); return; }
  authSet(n);
  redirect("/?m=pw");
}

// ------------------------------------------------------------ Hauptseite
static void colorRow(String& b, const char* name, const char* label, uint32_t c) {
  b += F("<div class=row><span class=n>"); b += label; b += F("</span><code data-l=");
  b += name; b += F("></code><input type=color name="); b += name;
  b += F(" value="); b += hexColor(c); b += F("></div>");
}

static void pageMain() {
  String b;
  b.reserve(6000);
  b += F("<h1>Tally32</h1>");
  String m = server.arg("m");
  if (m == "pw")     b += F("<p class=ok>Passwort ge&auml;ndert.</p>");
  if (m == "colors") b += F("<p class=ok>Farben gespeichert.</p>");
  if (m == "pin")    b += F("<p class=ok>PIN f&uuml;r das Men&uuml; am Display gespeichert.</p>");
  if (m == "pinerr") b += F("<p class=err>Der PIN muss genau 4 Ziffern haben.</p>");
  if (m == "disp")   b += F("<p class=ok>Anzeige gespeichert.</p>");
  if (m == "book")   b += F("<p class=ok>Terminbuchung gespeichert.</p>");
  if (m == "demo")   b += demoMode ? F("<p class=ok>Demomodus gestartet.</p>") : F("<p class=ok>Demomodus beendet.</p>");
  if (m == "lunerr") b += F("<p class=err>Mittagspause: Ende muss nach dem Beginn liegen.</p>");
  if (m == "bookerr") b += F("<p class=err>Terminbuchung: Ende muss mindestens 15 Minuten nach dem Beginn liegen, mindestens ein Wochentag.</p>");
  if (m == "wifi")   b += F("<p class=ok>WLAN-Passwort gespeichert. Der Access Point startet neu, bitte neu verbinden.</p>");
  if (m == "apon")   b += F("<p class=ok>Einrichtungs-WLAN eingeschaltet.</p>");

  b += F("<div class=card><h2>Status</h2>");
  if (demoMode) b += F("<p class=err>Demomodus aktiv.</p>");
  b += F("<p>Anzeige: <b>");
  b += modeName(cur.mode);
  if (cur.offline) { b += F(" (aus dem Kalender, Stand "); b += cur.stand; b += F(")"); }
  b += F("</b><br>Agent: ");
  b += linked ? F("verbunden") : F("<span class=err>nicht verbunden</span>");
  b += F("<br>Microsoft-Konto: ");
  b += account[0] ? esc(account) : String(F("nicht angemeldet"));
  b += F("</p></div>");

  b += F("<div class=card><h2>Office 365</h2>"
         "<p class=dim>Startet auf dem Computer eine neue Anmeldung. Den Code dann auf einem Ger&auml;t "
         "mit Internet eingeben (z.&nbsp;B. am PC). Code und Link stehen auch auf dem Display.</p>"
         "<div id=o365></div>");
  if (bookingEnabled && account[0] && !canWrite)
    b += F("<p class=err>F&uuml;r die Terminbuchung fehlt das Schreibrecht auf den Kalender. "
           "Bitte neu anmelden und dabei die Berechtigung best&auml;tigen.</p>");
  b += F("<form method=post action=/o365><button>Neue Anmeldung starten</button></form></div>");

  b += F("<div class=card><h2>Farben</h2><form method=post action=/colors>");
  colorRow(b, "free", "Frei", colors.free_);
  colorRow(b, "busy", "Besetzt", colors.busy);
  colorRow(b, "away", "Abwesend", colors.away);
  colorRow(b, "call", "Im Gespr&auml;ch", colors.call);
  b += F("<p class=dim>Tipp: Die Zust&auml;nde lassen sich am besten unterscheiden, wenn sich auch die "
         "Helligkeit deutlich unterscheidet (Prozentwert neben der Farbe). Die Schriftfarbe w&auml;hlt "
         "das Display automatisch.</p><button>Speichern</button></form>"
         "<form method=post action=/colors/reset><button class=sec>Standardfarben</button></form></div>");

  // Terminbuchung am Display
  char tf[8], tt[8];
  snprintf(tf, sizeof(tf), "%02u:%02u", bookFrom / 60, bookFrom % 60);
  snprintf(tt, sizeof(tt), "%02u:%02u", bookTo / 60, bookTo % 60);
  b += F("<div class=card><h2>Terminbuchung am Display</h2>"
         "<p class=dim>Besucher k&ouml;nnen am Display einen Termin von 15, 30 oder 60 Minuten in einer freien "
         "Zeit der laufenden oder kommenden Woche buchen und geben dazu einen Titel an. Sie erhalten eine "
         "Einladung per E-Mail.</p>"
         "<form method=post action=/booking><label><input type=checkbox name=on value=1");
  if (bookingEnabled) b += F(" checked");
  b += F("> Knopf &laquo;Termin buchen&raquo; anzeigen</label>"
         "<label for=bf>Fr&uuml;hester Beginn</label><input type=time id=bf name=from step=900 value=");
  b += tf;
  b += F("><label for=bt>Sp&auml;testes Ende</label><input type=time id=bt name=to step=900 value=");
  b += tt;
  char lf[8], lt[8];
  snprintf(lf, sizeof(lf), "%02u:%02u", lunchFrom / 60, lunchFrom % 60);
  snprintf(lt, sizeof(lt), "%02u:%02u", lunchTo / 60, lunchTo % 60);
  b += F("><label><input type=checkbox name=lon value=1");
  if (lunchOn) b += F(" checked");
  b += F("> Mittagspause nicht buchbar</label>"
         "<label for=lf>Mittagspause von</label><input type=time id=lf name=lfrom step=900 value=");
  b += lf;
  b += F("><label for=lt>Mittagspause bis</label><input type=time id=lt name=lto step=900 value=");
  b += lt;
  b += F("><label>Wochentage</label><div>");
  const char* wd[7] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };
  for (int i = 0; i < 7; i++) {
    b += F("<label style=display:inline-block;margin-right:12px><input type=checkbox name=d");
    b += i; b += F(" value=1");
    if (bookDays & (1 << i)) b += F(" checked");
    b += F("> "); b += wd[i]; b += F("</label>");
  }
  b += F("</div><button>Speichern</button></form></div>");

  b += F("<div class=card><h2>Demomodus</h2><p class=dim>Zeigt reihum alle Zust&auml;nde und f&uuml;hrt "
         "die Terminbuchung vor. Besucher k&ouml;nnen die Buchung auch selbst ausprobieren, es entsteht "
         "dabei kein echter Termin. Daten vom Computer werden so lange nicht angezeigt.</p>"
         "<form method=post action=/demo><input type=hidden name=on value=");
  b += demoMode ? F("0><button class=sec>Demomodus beenden") : F("1><button>Demomodus starten");
  b += F("</button></form></div>");

  b += F("<div class=card><h2>Anzeige</h2><form method=post action=/display>"
         "<label for=on>Name im Banner oben (wessen Kalender und Status das Display zeigt)</label>"
         "<input type=text id=on name=owner maxlength=40 value=\"");
  b += esc(ownerName.c_str());
  b += F("\"><p class=dim>Leer lassen zeigt das angemeldete Microsoft-Konto.</p>"
         "<label for=br>Helligkeit</label><input type=range id=br name=br min=12 max=255 value=");
  b += brightness;
  b += F("><label><input type=checkbox name=flip value=1");
  if (flip) b += F(" checked");
  b += F("> Display um 180&deg; drehen</label><button>Speichern</button></form></div>");

  b += F("<div class=card><h2>Men&uuml; am Display</h2><p class=dim>3 Sekunden auf das Tally32-Logo oben "
         "links dr&uuml;cken &ouml;ffnet die Einstellungen am Display, gesch&uuml;tzt mit diesem PIN "
         "(Standard 0000). Nach 5 falschen Versuchen ist das Men&uuml; eine Minute gesperrt.</p>"
         "<form method=post action=/pin><label for=pin>Neuer PIN (4 Ziffern)</label>"
         "<input id=pin name=pin inputmode=numeric pattern=[0-9]{4} maxlength=4 required autocomplete=off>"
         "<button>PIN speichern</button></form></div>");

  b += F("<div class=card><h2>Einrichtungs-WLAN</h2><p>Netz: <b>");
  b += ssid;
  b += apOff ? F("</b>, ausgeschaltet") : F("</b>, eingeschaltet");
  b += F("</p><form method=post action=/wifi>"
         "<label for=ap>Neues WLAN-Passwort (8 bis 63 Zeichen)</label>"
         "<input type=password id=ap name=ap autocomplete=new-password>"
         "<button>Speichern</button></form>"
         "<form method=post action=/ap><label><input type=checkbox name=off value=1");
  if (apOff) b += F(" checked");
  b += F("> Einrichtungs-WLAN abschalten</label>"
         "<p class=dim>Ohne Einrichtungs-WLAN ist das Webinterface nicht erreichbar, alle "
         "Einstellungen gehen weiter im Men&uuml; am Display. Wieder einschalten: 5 Sekunden aufs "
         "Display dr&uuml;cken.</p><button>Speichern</button></form></div>");

  b += F("<div class=card><h2>Zugang</h2><a class=btn href=/password>Passwort &auml;ndern</a> "
         "<form method=post action=/logout style=display:inline><button class=sec>Abmelden</button></form></div>");

  b += F("<script>"
         "function lum(h){const r=parseInt(h.substr(1,2),16),g=parseInt(h.substr(3,2),16),b=parseInt(h.substr(5,2),16);"
         "return Math.round((0.299*r+0.587*g+0.114*b)/2.55)}"
         "document.querySelectorAll('input[type=color]').forEach(i=>{const l=document.querySelector('code[data-l='+i.name+']');"
         "const u=()=>l.textContent=i.value.toUpperCase()+'  Helligkeit '+lum(i.value)+' %';u();i.addEventListener('input',u)});"
         "const o=document.getElementById('o365');"
         "function e(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}"
         "async function poll(){try{const j=await (await fetch('/api/o365')).json();let h='';"
         "if(j.st=='requested')h='<p>Warte auf den Agenten &hellip;</p>';"
         "else if(j.st=='code')h='<p>&Ouml;ffne <b>'+e(j.url)+'</b> und gib diesen Code ein:</p><p class=code>'+e(j.code)+'</p>';"
         "else if(j.st=='ok')h='<p class=ok>Angemeldet als '+e(j.msg)+'</p>';"
         "else if(j.st=='err')h='<p class=err>'+e(j.msg)+'</p>';"
         "o.innerHTML=h}catch(x){}setTimeout(poll,2000)}poll();"
         "</script>");
  sendPage("Tally32", b);
}

static void apiO365() {
  if (!loggedIn()) { server.send(401, "application/json", "{}"); return; }
  const char* st = "idle";
  switch (login365.st) {
    case L_REQUESTED: st = "requested"; break;
    case L_CODE:      st = "code"; break;
    case L_OK:        st = "ok"; break;
    case L_ERR:       st = "err"; break;
    default: break;
  }
  auto js = [](const char* s) {               // minimales JSON-Escaping
    String o;
    for (; *s; s++) {
      if (*s == '"' || *s == '\\') { o += '\\'; o += *s; }
      else if (*s == '_') o += ' ';
      else if ((uint8_t)*s >= 0x20) o += *s;
    }
    return o;
  };
  String j = "{\"st\":\"" + String(st) + "\",\"code\":\"" + js(login365.code) + "\",\"url\":\"" +
             js(login365.url) + "\",\"msg\":\"" + js(login365.msg) + "\"}";
  server.send(200, "application/json", j);
}

// ------------------------------------------------------------ Aktionen
static void handleColors() {
  if (!guard()) return;
  Colors c = colors;
  parseColor(server.arg("free"), c.free_);
  parseColor(server.arg("busy"), c.busy);
  parseColor(server.arg("away"), c.away);
  parseColor(server.arg("call"), c.call);
  colors = c;
  colorsSave();
  redirect("/?m=colors");
}

static void handleColorsReset() {
  if (!guard()) return;
  colors = DEFAULT_COLORS;
  colorsSave();
  redirect("/?m=colors");
}

static void handleDisplay() {
  if (!guard()) return;
  int br = server.arg("br").toInt();
  brightness = br < 12 ? 12 : (br > 255 ? 255 : br);
  flip = server.arg("flip") == "1";
  String owner = server.arg("owner"); owner.trim();
  if (owner.length() > 40) owner = owner.substring(0, 40);
  ownerName = owner;
  displayApply(true);
  redirect("/?m=disp");
}

static int parseHHMM(const String& s) {          // "08:30" -> 510, Fehler -> -1
  int h, m;
  if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

static void handleBooking() {
  if (!guard()) return;
  int from = parseHHMM(server.arg("from")), to = parseHHMM(server.arg("to"));
  uint8_t mask = 0;
  for (int i = 0; i < 7; i++) if (server.arg("d" + String(i)) == "1") mask |= 1 << i;
  int lf = parseHHMM(server.arg("lfrom")), lt = parseHHMM(server.arg("lto"));
  bool lon = server.arg("lon") == "1";
  if (from < 0 || to < 0 || to - from < 15 || !mask) { redirect("/?m=bookerr"); return; }
  if (lon && (lf < 0 || lt < 0 || lt <= lf)) { redirect("/?m=lunerr"); return; }
  lunchOn = lon;
  if (lf >= 0 && lt > lf) { lunchFrom = lf; lunchTo = lt; }
  bookingEnabled = server.arg("on") == "1";
  bookFrom = from;
  bookTo = to;
  bookDays = mask;
  bookingSave();
  redirect("/?m=book");
}

static void handlePin() {
  if (!guard()) return;
  String pin = server.arg("pin");
  if (!pinValid(pin)) { redirect("/?m=pinerr"); return; }
  pinSave(pin);
  redirect("/?m=pin");
}

static void handleWifi() {
  if (!guard()) return;
  String ap = server.arg("ap");
  if (ap.length() < 8 || ap.length() > 63) { redirect("/"); return; }
  apPassSave(ap);
  redirect("/?m=wifi");
  delay(500);
  if (apRunning) WiFi.softAP(ssid, ap.c_str());  // AP mit neuem Passwort neu starten
}

static void handleAp() {
  if (!guard()) return;
  bool off = server.arg("off") == "1";
  if (off == apOff) { redirect("/"); return; }
  apOff = off;
  apOffSave();
  if (!off) { apApply(); redirect("/?m=apon"); return; }
  String b = F("<h1>Tally32</h1><div class=card><h2>Einrichtungs-WLAN</h2>"
               "<p class=ok>Das Einrichtungs-WLAN wird abgeschaltet.</p>");
  b += F("<p>Wieder einschalten: 5 Sekunden aufs Display dr&uuml;cken.</p></div>");
  sendPage("Tally32", b);
  delay(1500);                                   // Seite noch ausliefern
  apApply();
}

static void handleDemo() {
  if (!guard()) return;
  demoMode = server.arg("on") == "1";
  demoSave();
  redirect("/?m=demo");
}

static void handleO365() {
  if (!guard()) return;
  login365Start();
  redirect("/");
}

static void handleLogout() {
  session = "";
  redirect("/login");
}

// ------------------------------------------------------------ Access Point
// Startet bzw. stoppt den Access Point gemaess apOff.
void apApply() {
  if (apOff) {
    if (apRunning) { dns.stop(); WiFi.softAPdisconnect(true); apRunning = false; }
    WiFi.mode(WIFI_OFF);
  } else if (!apRunning) {
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid, apPassLoad().c_str())) { apFailed = true; return; }
    apFailed = false;
    dns.start(53, "*", WiFi.softAPIP());
    apRunning = true;
  }
}

// ------------------------------------------------------------ Start
void webBegin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);        // geht auch ohne laufenden AP
  snprintf(ssid, sizeof(ssid), "Tally32-%02X%02X", mac[4], mac[5]);
  // TCP/IP-Stack immer starten: Ist das Einrichtungs-WLAN aus, startet ihn sonst
  // niemand, und server.begin() stuerzt beim Oeffnen des Sockets ab (assert in
  // xQueueSemaphoreTake, Bootloop). Mehrfacher Aufruf ist harmlos.
  esp_netif_init();
  apOffLoad();
  apApply();

  const char* hdrs[] = { "Cookie" };
  server.collectHeaders(hdrs, 1);

  server.on("/", HTTP_GET, [] { if (guard()) pageMain(); });
  server.on("/login", HTTP_GET, [] { pageLogin(nullptr); });
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/password", HTTP_GET, [] { if (guard(true)) pagePassword(nullptr, false); });
  server.on("/password", HTTP_POST, handlePasswordPost);
  server.on("/colors", HTTP_POST, handleColors);
  server.on("/colors/reset", HTTP_POST, handleColorsReset);
  server.on("/display", HTTP_POST, handleDisplay);
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/ap", HTTP_POST, handleAp);
  server.on("/booking", HTTP_POST, handleBooking);
  server.on("/pin", HTTP_POST, handlePin);
  server.on("/demo", HTTP_POST, handleDemo);
  server.on("/o365", HTTP_POST, handleO365);
  server.on("/api/o365", HTTP_GET, apiO365);
  server.on("/logout", HTTP_POST, handleLogout);
  server.onNotFound([] {                         // Captive Portal: alles auf die Startseite
    server.sendHeader("Location", "http://" + server.client().localIP().toString() + "/");
    server.send(302, "text/plain", "");
  });
  server.begin();
}

void webLoop() {
  if (apRunning) dns.processNextRequest();
  server.handleClient();
}
