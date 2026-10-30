<#
  install_windows.ps1 - Tally32-Agent unter Windows einrichten

  Legt (falls noetig) die Python-venv an, installiert die Pakete und
  registriert eine Aufgabe in der Aufgabenplanung: Start bei der Anmeldung,
  unsichtbar (pythonw), Neustart nach Absturz. Log: %LOCALAPPDATA%\tally32\tally32.log

  Aufruf (PowerShell, im Ordner agent):
    powershell -ExecutionPolicy Bypass -File .\install_windows.ps1
    powershell -ExecutionPolicy Bypass -File .\install_windows.ps1 -Uninstall
#>
param([switch]$Uninstall)
$ErrorActionPreference = "Stop"

$Here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$TaskName = "Tally32 Agent"
$LogDir   = Join-Path $env:LOCALAPPDATA "tally32"
$LogFile  = Join-Path $LogDir "tally32.log"
$Venv     = Join-Path $Here ".venv"
$PyW      = Join-Path $Venv "Scripts\pythonw.exe"
$Py       = Join-Path $Venv "Scripts\python.exe"
$Agent    = Join-Path $Here "tally_agent.py"

if ($Uninstall) {
  Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
  Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
  Get-CimInstance Win32_Process -Filter "Name='pythonw.exe'" |
    Where-Object { $_.CommandLine -like "*tally_agent.py*" } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
  Write-Host "Tally32-Agent entfernt."
  exit 0
}

# Python-venv anlegen, falls sie fehlt
if (-not (Test-Path $Py)) {
  $launcher = Get-Command py -ErrorAction SilentlyContinue
  if ($launcher) { & py -3 -m venv $Venv } else { & python -m venv $Venv }
  if (-not (Test-Path $Py)) { throw "Python 3 nicht gefunden. Bitte Python 3 installieren (python.org oder Microsoft Store)." }
}
& $Py -m pip install --quiet --upgrade pip
& $Py -m pip install --quiet -r (Join-Path $Here "requirements.txt")

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

# Laufende Instanz beenden, Aufgabe neu registrieren
Get-CimInstance Win32_Process -Filter "Name='pythonw.exe'" |
  Where-Object { $_.CommandLine -like "*tally_agent.py*" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue

$action    = New-ScheduledTaskAction -Execute $PyW -Argument "`"$Agent`" --log `"$LogFile`"" -WorkingDirectory $Here
$trigger   = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
               -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
               -MultipleInstances IgnoreNew
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal | Out-Null
Start-ScheduledTask -TaskName $TaskName

Write-Host "Tally32-Agent laeuft (Aufgabe '$TaskName'). Log: $LogFile"
