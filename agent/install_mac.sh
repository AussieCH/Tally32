#!/bin/zsh
# Richtet den Tally32-Agenten als LaunchAgent ein: startet bei der Anmeldung
# und wird bei einem Absturz neu gestartet. Aufruf: agent/install_mac.sh
# Entfernen: agent/install_mac.sh uninstall
# Mehrere Displays an einem Rechner: je Display ein Agent mit eigener Nummer und der
# USB-Seriennummer des Displays (python -m serial.tools.list_ports -v):
#   agent/install_mac.sh display 2 sn:12:34:56:78:9A:BC
#   agent/install_mac.sh uninstall 2
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
LABEL="ch.aussie.tally32"
LOG="$HOME/Library/Logs/tally32.log"
PORTARGS=""
if [[ "$1" == "display" ]]; then                 # weiterer Agent fuer ein bestimmtes Display
  LABEL="$LABEL-$2"; LOG="$HOME/Library/Logs/tally32-$2.log"
  PORTARGS="<string>--port</string><string>$3</string>"
elif [[ "$1" == "uninstall" && -n "$2" ]]; then
  LABEL="$LABEL-$2"
fi
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"

if [[ "$1" == "uninstall" ]]; then
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
  rm -f "$PLIST"
  echo "Tally32-Agent entfernt."
  exit 0
fi

[[ -x "$HERE/.venv/bin/python" ]] || { echo "Zuerst die venv anlegen (siehe README)."; exit 1; }

mkdir -p "$HOME/Library/LaunchAgents"
cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$HERE/.venv/bin/python</string>
    <string>-u</string>
    <string>$HERE/tally_agent.py</string>
    $PORTARGS
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>10</integer>
  <key>ProcessType</key><string>Background</string>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict>
</plist>
EOF

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "Tally32-Agent laeuft. Log: $LOG"
