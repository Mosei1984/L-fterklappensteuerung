# Lueftersteuerung Konfigurator

Windows-Service-Tool fuer die Luefterklappensteuerung eines 80-mm-Rohrventils.
Die normale Bedienung laeuft in `Luefterklappen-Konfigurator.exe` als eigenes
Windows-Fenster; der lokale Host wird intern im Hintergrund gestartet.

## Voraussetzungen

- .NET 8 SDK
- Windows, Linux oder macOS fuer die Kernmodule
- Windows empfohlen fuer den ersten UF2-/COM-Port-Test

Falls kein globales SDK installiert ist, kann ein lokales SDK verwendet werden:

```powershell
$env:DOTNET_ROOT='C:\Users\mosei\.dotnet8'
$env:DOTNET_CLI_HOME=(Resolve-Path ..\..).Path
& 'C:\Users\mosei\.dotnet8\dotnet.exe' --info
```

## Expert-Host Start

```powershell
$env:DOTNET_ROOT='C:\Users\mosei\.dotnet8'
$env:DOTNET_CLI_HOME=(Resolve-Path ..\..).Path
& 'C:\Users\mosei\.dotnet8\dotnet.exe' run --project .\src\LuefterConfigurator.Host
```

Direktes Oeffnen von <http://127.0.0.1:5184> ist nur fuer Entwicklung,
Host-Smoke-Tests und Experten gedacht. Normale Nutzer starten die Windows-App.

Die Startseite priorisiert den Loxone Setup Wizard. USB Host Test, Gateway-Test
und Rohdiagnose liegen separat im Expertentest-Bereich.

## Lokale API

Die eingebettete Windows-UI ruft dieselben Endpunkte auf, die auch in den
Integrationstests laufen:

- `GET /api/controllers/state`
- `POST /api/controllers/scan`
- `POST /api/controllers/connect`
- `PUT /api/controllers/config`
- `POST /api/profiles/import`
- `POST /api/commands/{home|open|half|safe|refresh-machine}`
- `POST /api/gateway/{start|stop}`
- `POST /api/firmware/check`
- `POST /api/firmware/flash` mit Multipart-Feld `file` (`.uf2`)
- `POST /api/exports`
- `GET /api/exports`
- `GET /api/exports/{dateiname}`

Der `safe`-Befehl schreibt nur den Safe-Wert in den Servicezustand. Eine echte Anfahrt muss weiterhin bewusst ueber Ziel-/Bewegungskommandos erfolgen.
Der `refresh-machine`-Befehl sendet `REFRESH`: Fehler quittieren und neu
referenzieren, ohne den Controller per MCU-Reset neu zu booten.

Im normalen Hostmodus scannt `POST /api/controllers/scan` die lokalen COM-Ports passiv mit `ID?` und `SAFE?`.
Im Testmodus wird ein Offline-Discovery verwendet, damit die Tests ohne angeschlossene Hardware deterministisch bleiben.
Konfigurationsschreiben ist bis zum Hardwareaufbau bewusst auf Servicezustand und Befehlslog begrenzt; der passive Scan greift bereits auf den Pico zu.

## UF2 Modus

`POST /api/firmware/check` sucht automatisch nach einem gemounteten Pico-UF2-Laufwerk (`INFO_UF2.TXT`).
Wenn der Pico nicht im BOOTSEL/UF2-Modus ist, liefert die API einen sichtbaren Fehlerzustand und die UI zeigt den Fehler im Firmware-Panel an.
Der Configurator setzt den Pico aktuell nicht automatisch in den BOOTSEL-Modus; das bleibt bewusst manuell oder einem spaeteren expliziten Firmware-Kommando vorbehalten.

Der Button `UF2 flashen` oeffnet eine `.uf2` Dateiauswahl und kopiert die Datei danach automatisch auf das erkannte UF2-Laufwerk. Nach erfolgreichem Kopieren startet der Pico normalerweise selbst neu; waehrenddessen verschwindet das UF2-Laufwerk kurz.

API-Beispiel:

```powershell
curl.exe -F "file=@.\firmware.uf2" http://127.0.0.1:5184/api/firmware/flash
```

## Exportziel

Home-Automation-Exporte werden als Dateien geschrieben nach:

```text
tools/configurator/src/LuefterConfigurator.Host/App_Data/exports
```

Beim portablen Build liegt derselbe Ordner relativ neben der gestarteten EXE:

```text
App_Data/exports
```

Erzeugte Loxone-Dateien fuer Controller-ID `13`:

- `MB_Luefterklappe_FanFlap_ID13.xml`: Loxone-Modbus-Template/Einrichtungsdaten fuer die `Comm`-Vorlage.
- `loxone-fanflap-13.json`: maschinenlesbarer Register-, Gateway- und Profilvertrag.
- `loxone-fanflap-13.md`: kurze Einrichtungs- und Registerdoku.

Weitere Adapter erzeugen Home-Assistant MQTT Discovery Text, openHAB Things und die generische Modbus-Registerdoku.
Die UI zeigt die Dateien nach `Export testen` direkt als Downloadlinks. Die API liefert dieselben Dateien ueber `GET /api/exports` und `GET /api/exports/{dateiname}` aus. Pfad-Traversal wird abgewiesen; es koennen nur vorher erzeugte Dateien aus dem Exportordner geladen werden.

Loxone-Hinweise:

- Datei `MB_Luefterklappe_FanFlap_ID<id>.xml` in den Loxone-Config-Template-Ordner `C:\ProgramData\Loxone\Loxone Config <version>\ENG\Comm` kopieren oder ueber die Template-Importfunktion einlesen.
- Loxone IO-Adressen sind 0-basiert und entsprechen direkt den Firmware-Holding-Registern `0..16`.
- Fuer die Luefterklappe zuerst Register `14` als Analogaktor `0..1000` und Register `8`, `9`, `15`, `16` als langsam gepollte Status-/Parameterpunkte verwenden.
- Register `0` mit Wert `5` als `refreshMachine`-Kommando anlegen. Das ist der
  bevorzugte Bedienbefehl nach Blockade, Endschalterfehler oder Safe-State:
  Fehler quittieren und Homing starten, ohne den Pico neu zu resetten.
- 32-bit Step-Register liegen High-Word zuerst, Low-Word danach. Fuer normale Home-Automation-Steuerung ist `target_promille` auf Register `14` robuster.

## Portable Build

Portable Windows-App bauen:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\configurator\publish-portable.ps1
```

Ausgabe:

```text
artifacts/configurator-portable/win-x64
```

Normaler Start in diesem Ordner:

```powershell
.\Luefterklappen-Konfigurator.exe
```

Normale Nutzer starten die Windows-App `Luefterklappen-Konfigurator.exe`; sie
oeffnet ein eigenes Fenster und startet den lokalen Host unsichtbar im
Hintergrund.

Expertenstart des internen Hosts in diesem Ordner:

```powershell
.\LuefterConfigurator.Host.exe
```

## Windows Installation

Installierbares Windows-Paket bauen:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\configurator\build-windows-installer.ps1
```

Ausgabe:

```text
artifacts/configurator-installer/win-x64
```

Installation aus dem Payload-Ordner fuer normale Nutzer:

```powershell
cd .\artifacts\configurator-installer\win-x64\payload
.\Luefterklappen-Konfigurator-Setup.cmd
```

Der Setup-Wizard zeigt die EULA, fragt den Installationsordner ab, bietet eine
Desktop-Verknuepfung an und kann den Konfigurator direkt nach der Installation
starten. Der Zielordner ist ohne Adminrechte standardmaessig
`%LOCALAPPDATA%\Programs\LuefterConfigurator`.

Silent-Installation fuer Experten:

```powershell
powershell -ExecutionPolicy Bypass -File .\install-windows.ps1 -AcceptEula
```

Die Installation legt die Windows-App unter `%LOCALAPPDATA%\Programs\LuefterConfigurator` ab,
schreibt `INSTALL_STATUS.json`, erstellt Startmenueeintraege fuer Start und
Deinstallation und registriert den Uninstaller unter
`HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall`.
Das Paket enthaelt `Luefterklappen-Konfigurator.ico`; dieses Icon wird fuer den
Startmenueintrag, den Apps-und-Features-Eintrag und den Installationsordner
mitkopiert. Die Browser-UI nutzt die passenden SVG/PNG-Logoassets aus
`wwwroot/brand`.

Deinstallation:

```powershell
powershell -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\LuefterConfigurator\uninstall-windows.ps1"
```

Ohne `-Quiet -Force` fragt der Uninstaller interaktiv nach Bestaetigung. Aus
Windows Apps & Features wird derselbe interaktive Deinstallationspfad genutzt.

## Test

```powershell
$env:DOTNET_ROOT='C:\Users\mosei\.dotnet8'
$env:DOTNET_CLI_HOME=(Resolve-Path ..\..).Path
& 'C:\Users\mosei\.dotnet8\dotnet.exe' test .\LuefterConfigurator.sln
```

Der harte Repo-Gate erzeugt zusaetzlich TRX, Cobertura-Coverage,
MSBuild-/Razor-Binlogs und Markdownlint-/LSP-Log-Artefakte:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ..\..\tools\run_hard_checks.ps1
```

## Firmware-Quality

Auf Windows sollte PlatformIO mit kurzem Core-Pfad laufen, damit Arduino-Mbed-Pakete nicht an Pfadlaengen scheitern:

```powershell
$env:PATH='C:\Users\mosei\AppData\Local\Programs\Python\Python312\Scripts;' + $env:PATH
$env:PLATFORMIO_CORE_DIR='C:\pio-luefter'
powershell -ExecutionPolicy Bypass -File ..\..\tools\run_quality_checks.ps1
```

## Release-1 Umfang

- USB CDC Textprotokoll-Grundlage
- Modbus RTU CRC und Request-Grundlage
- mehrere Controller-Sessions mit ID-Konflikterkennung
- Modbus TCP Gateway Core
- UF2 Firmware Update Workflow
- JSON Profilimport
- Loxone/Home Assistant/openHAB/Modbus Exporte
- eingebettete Windows-UI mit Health- und Configurator-API
