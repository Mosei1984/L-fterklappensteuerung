# Luefterklappensteuerung

MCU-unabhaengige Steuerlogik fuer ein 80-mm-Rohrventil mit Schrittmotor,
TMC2209-UART, RS485/Modbus RTU und zwei Endschaltern. Die eigentliche
State-Machine liegt in einem portablen Core; `src/main.cpp` ist nur die
Raspberry-Pi-Pico/Arduino-HAL.

## Funktionsliste

- Portable C++14-Core-Logik ohne Arduino-Abhaengigkeit.
- Homing gegen Min-/Max-Endschalter mit Plausibilitaetspruefung und
  StallGuard-Redundanz, wenn ein Endschalter nicht ausloest.
- Soft-Endstops und Zielpositionen in Steps oder `0..1000` Promille.
- Safe-Position in Promille; nach gueltigem Homing faehrt die Klappe in diese
  definierte Lueftungsstellung.
- Fehlerzustand bei unerwartetem Endschalterereignis, Blockade oder StallGuard
  waehrend normaler Bewegung.
- Reset-Timeout mit automatischem Re-Homing.
- TMC2209-UART mit Datagramm-CRC, Konfiguration und StallGuard-Abfrage.
- Modbus-RTU-Slave fuer Loxone/Home-Automation auf RS485.
- Geraete-ID `1..247` und Safe-Position werden im Pico-Flash mit CRC
  persistiert.
- Altes adressiertes Textprotokoll als Service-/Debugpfad.
- Native Unit-Tests mit Fake-Stepper, Fake-UART, Fake-Zeit und Fake-Events.
- clang-tidy, cppcheck und MISRA-Addon-Checks ueber das Quality-Skript.
- sigrok-cli Logic-Analyzer-Testpfad fuer Modbus, TMC-UART und STEP-Timing.
- Lokaler C# Konfigurator unter `tools/configurator` mit Loxone Setup Wizard,
  Exporte, JSON-Profile, UF2-Flashing, Logo/Icon-Paket und Modbus-TCP-Gateway.
- Mermaid-Architektur- und Wiring-Diagramme im README und als `docs/diagrams/*.mmd`.
- Pico-Onboard-LED als Statusanzeige: Homing blinkt langsam, Ready leuchtet,
  Fehler blinkt schnell.

## Architektur

```mermaid
flowchart TB
  subgraph Home["Home automation"]
    Loxone["Loxone Modbus Extension<br/>RS485 master"]
  end

  subgraph PicoHal["Pico HAL: src/main.cpp"]
    Usb["USB Serial<br/>debug events"]
    Rs485Hal["Serial1 RS485<br/>GP0/GP1, 38400 8N1"]
    TmcHal["TMC UART HAL<br/>GP8/GP9, 115200 8N1"]
    StepperHal["AccelStepper HAL<br/>STEP/DIR/ENABLE"]
    InputHal["GPIO inputs<br/>MIN/MAX endstops"]
    FlashHal["FlashIAP settings<br/>last flash sector"]
  end

  subgraph Core["MCU independent core: lib/Luefterklappe/src"]
    Modbus["ModbusRtuServer<br/>CRC16, address filter, registers"]
    Controller["FanFlapController<br/>state machine, homing, faults"]
    Tmc["Tmc2209Driver<br/>UART datagrams, CRC, StallGuard"]
    Settings["PersistentSettings<br/>magic, version, CRC"]
    Ports["IoPorts interfaces<br/>StepperPort, UartPort, DelayPort, EventSink"]
  end

  subgraph Hardware["Field hardware"]
    Driver["TMC2209 stepper driver"]
    Motor["Stepper motor"]
    MinMax["Min/Max switches"]
    Rs485Bus["2-wire RS485 bus"]
  end

  Loxone <-->|"Modbus RTU"| Rs485Bus
  Rs485Bus <-->|"A/B + GND"| Rs485Hal
  Rs485Hal --> Modbus
  Modbus --> Controller
  Modbus --> Settings
  Settings --> FlashHal
  Controller --> StepperHal
  Controller --> Tmc
  Tmc --> TmcHal
  TmcHal <-->|"UART"| Driver
  StepperHal -->|"STEP/DIR/ENABLE"| Driver
  Driver --> Motor
  MinMax --> InputHal
  InputHal --> Controller
  Controller --> Usb
  Controller -. uses .-> Ports
  Modbus -. uses .-> Ports
  Tmc -. uses .-> Ports
```

### Komponenten

| Pfad | Aufgabe |
| --- | --- |
| `lib/Luefterklappe/src/IoPorts.h` | MCU-unabhaengige Ports fuer Stepper, UART, Delay und Events |
| `lib/Luefterklappe/src/FanFlapController.*` | State-Machine, Homing, Softlimits, Fehlerbehandlung, Textbefehle |
| `lib/Luefterklappe/src/Tmc2209Driver.*` | TMC2209-UART-Datagramme, CRC, Konfiguration, StallGuard |
| `lib/Luefterklappe/src/ModbusRtuServer.*` | Modbus-RTU-Slave, Registermap, CRC16, Exception Responses |
| `lib/Luefterklappe/src/PersistentSettings.*` | Persistente ID/Safe-Position mit Magic, Version und CRC |
| `src/main.cpp` | Pico/Arduino-HAL, Pinning, Serial1 RS485, TMC-UART, GPIO |
| `test/test_controller/test_main.cpp` | Native Unit-Tests fuer Core, Modbus und TMC |
| `tools/la/` | sigrok-cli Capture, Decode und Offline-Analyse |
| `tools/configurator/` | Lokaler C# Service mit Browser-UI fuer Setup, Diagnose und Exporte |
| `docs/diagrams/` | Mermaid-Quellen fuer Architektur und Wiring |

## Konfigurator

Der PC-Konfigurator ist das primaere Service-Tool fuer die Inbetriebnahme des
80-mm-Rohrventils. Er startet lokal auf `http://127.0.0.1:5184` und fuehrt
normale Nutzer zuerst durch den **Loxone Setup Wizard**:

1. Loxone/Modbus-TCP-Daten vorbereiten.
2. Pico ueber USB erkennen.
3. UF2-Firmware pruefen und flashen.
4. Controller-ID, Safe-Position und Grenzen schreiben.
5. Loxone XML, JSON-Konfig und Markdown-Doku erzeugen und herunterladen.
6. Abschlusscheck fuer Statusregister und Gateway durchlaufen.

USB Host Test, Rohbefehle und Gateway-Diagnose bleiben in einer getrennten
Expertentest-Spalte. Die Windows-Paketierung erzeugt ein sichtbares App-Icon
fuer EXE, Startmenue, Installationsordner und Browser-Favicon.

Start aus dem Quellbaum:

```powershell
$env:DOTNET_ROOT='C:\Users\mosei\.dotnet8'
$env:DOTNET_CLI_HOME=(Resolve-Path .\.dotnet-cli-home).Path
& 'C:\Users\mosei\.dotnet8\dotnet.exe' run --project .\tools\configurator\src\LuefterConfigurator.Host
```

Windows Installation bauen:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\configurator\build-windows-installer.ps1
```

Das Paket liegt danach unter:

```text
artifacts/configurator-installer/win-x64/Luefterklappen-Konfigurator-win-x64.zip
```

Nach dem Entpacken startet `Luefterklappen-Konfigurator-Setup.cmd` den
interaktiven Windows-Setup-Wizard mit EULA-Bestaetigung, Installationspfad,
Desktop-Verknuepfung und optionalem Start nach der Installation. Die
Deinstallation laeuft ueber Apps & Features oder den Startmenueeintrag
`Luefterklappen Konfigurator deinstallieren`.

## Komplettes Wiring

```mermaid
flowchart LR
  Pico["Raspberry Pi Pico<br/>Arduino-Pico firmware"]
  Rs485["Waveshare Pico-2CH-RS485<br/>CH0 only"]
  Loxone["Loxone Modbus Extension<br/>RS485 master"]
  Tmc["TMC2209 driver module"]
  Motor["Stepper motor"]
  Min["MIN endstop<br/>NO switch to GND"]
  Max["MAX endstop<br/>NO switch to GND"]
  Psu["Motor PSU<br/>according to driver and motor"]
  La["Logic analyzer<br/>optional"]
  Usb["USB host<br/>debug serial 115200"]

  Pico -- "GP0 Serial1 TX" --> Rs485
  Rs485 -- "GP1 Serial1 RX" --> Pico
  Rs485 <-->|"A/B twisted pair + GND<br/>38400 8N1"| Loxone

  Pico -- "GP2 STEP" --> Tmc
  Pico -- "GP3 DIR" --> Tmc
  Pico -- "GP7 ENABLE active low" --> Tmc
  Pico -- "GP8 UART TX" --> Tmc
  Tmc -- "GP9 UART RX" --> Pico
  Pico -- "3V3 VIO + GND" --> Tmc
  Psu -- "VMOT + GND" --> Tmc
  Tmc -- "A1/A2/B1/B2" --> Motor

  Min -- "GP5 input pullup<br/>active low" --> Pico
  Max -- "GP6 input pullup<br/>active low" --> Pico
  Usb <-->|"USB CDC"| Pico

  La -. "D0: GP0 RS485 TX" .-> Pico
  La -. "D1: GP1 RS485 RX" .-> Pico
  La -. "D2: GP2 STEP" .-> Pico
  La -. "D3: GP3 DIR" .-> Pico
  La -. "D4: GP5 MIN" .-> Pico
  La -. "D5: GP6 MAX" .-> Pico
  La -. "D6: GP7 ENABLE" .-> Pico
  La -. "D7: GP8 TMC TX<br/>or GP9 TMC RX in second run" .-> Pico
```

### Pin-Tabelle

| Pico Pin | Richtung | Verbindung | Pegel/Protokoll | Hinweis |
| --- | --- | --- | --- | --- |
| `GP0` | TX | Waveshare RS485 CH0 TX/DI | UART `38400 8N1` | `Serial1 TX`, Modbus-Antworten |
| `GP1` | RX | Waveshare RS485 CH0 RX/RO | UART `38400 8N1` | `Serial1 RX`, Modbus-Anfragen |
| `GP2` | OUT | TMC2209 `STEP` | 3.3 V digital | AccelStepper STEP |
| `GP3` | OUT | TMC2209 `DIR` | 3.3 V digital | AccelStepper DIR |
| `GP5` | IN | Min-Endschalter nach GND | `INPUT_PULLUP`, aktiv LOW | Firmware erwartet NO-Schalter nach GND |
| `GP6` | IN | Max-Endschalter nach GND | `INPUT_PULLUP`, aktiv LOW | Firmware erwartet NO-Schalter nach GND |
| `GP7` | OUT | TMC2209 `EN`/`ENABLE` | aktiv LOW | LOW = Treiber aktiv |
| `GP8` | TX | TMC2209 `PDN_UART`/UART RX | UART `115200 8N1` | Pico -> TMC, typ. ueber 1 kOhm |
| `GP9` | RX | TMC2209 `PDN_UART`/UART TX | UART `115200 8N1` | TMC -> Pico; bei 1-Draht-UART gleiche Leitung |
| `3V3` | PWR | TMC2209 `VIO` | 3.3 V | Nur Logikversorgung, nicht Motorversorgung |
| `GND` | PWR | RS485, TMC, Endschalter, PSU, LA | gemeinsame Masse | Pflicht fuer stabile Kommunikation |
| `USB` | I/O | PC/Service | CDC Serial `115200` | Debug-Events und Servicebefehle |
| `LED_BUILTIN` | OUT | Pico-Onboard-LED | Status | Homing langsam blinkend, Ready an, Fehler schnell blinkend |

### Feldbus und Versorgung

- RS485 A/B als verdrillte Leitung fuehren; GND mitfuehren.
- Abschlusswiderstand nur an den zwei physischen Busenden setzen.
- Bias/Failsafe nur einmal am Bus, falls Master/Modul das nicht bereits stellt.
- Wenn Modbus nicht antwortet, zuerst A/B tauschen und GND pruefen.
- Motorversorgung `VMOT` passend zu Motor und TMC-Modul auslegen.
- TMC2209-Stromlimit am Modul korrekt einstellen; Firmware ersetzt keine
  elektrische Strombegrenzung.
- Endschalter sind aktuell als NO-Schalter nach GND vorgesehen. Fuer NC-
  Sicherheitsschalter muss die HAL-Logik angepasst oder extern invertiert werden.
- RS485 A/B nicht direkt mit einem 3.3-V-Logic-Analyzer messen. Fuer UART-
  Decoding an GP0/GP1 bzw. DI/RO messen.

### BTT TMC2209 V1.3

Der verlinkte BIGTREETECH/BTT TMC2209 V1.3 passt zum vorgesehenen UART-Modus.
Die Firmware nutzt weiterhin STEP/DIR fuer die Bewegung und UART fuer
Konfiguration/Diagnose/StallGuard. Praktische Punkte fuer dieses Modul:

- `EN` ist aktiv LOW; in der Firmware ist `GP7` entsprechend aktiv LOW.
- `PDN_UART` ist die UART-Leitung. Bei Ein-Draht-UART `GP8` ueber etwa `1 kOhm`
  auf `PDN_UART` fuehren und `GP9` an dieselbe Leitung bzw. an den UART-Ausgang
  des Moduls, falls das Carrier-Board TX/RX trennt.
- `VIO` an `3V3`, `GND` gemeinsam mit Pico, RS485 und Motorversorgung.
- Kuehlkoerper montieren und Motorstrom/Vref am Modul passend zum Motor setzen.
  Firmware-UART ersetzt keine elektrische Strombegrenzung.
- StallGuard4 ist als Zusatzdiagnose aktiv, mechanische Endschalter bleiben in
  dieser Steuerung die primaere Referenz fuer Home-Betrieb.

## Safe-Zustand fuer Wohnraumlueftung

Normative Quellen fuer Wohnraumlueftung fordern keine pauschale "Klappe zu"-
Fehlerstellung. Die sicherere Firmware-Annahme ist: Luftwechsel erhalten, aber
keine Kraft gegen eine Blockade aufbauen. Deshalb gilt:

- Nach gueltigem Homing faehrt die Steuerung die konfigurierbare Safe-Position
  an. Default ist `1000` Promille, also offen.
- Im Fehlerfall stoppt der Motor, der Treiber wird deaktiviert. Danach ist
  `REFRESH` der bevorzugte Weg: Fehler quittieren, Maschine neu referenzieren,
  aber MCU/Pico nicht neu booten. `RESET` bleibt als Kompatibilitaetsbefehl
  erhalten.
- StallGuard wird beim Homing nur redundant benutzt: Wenn der jeweilige
  Endschalter nicht ausloest, darf StallGuard die mechanische Endlage erkennen.
- Bei normaler Bewegung und beim kurzen Wegfahr-Free-Check bedeutet StallGuard
  Blockade/Ueberlast und fuehrt in den Fehlerzustand.

Hintergrundquellen:

- DIN FAQ zur `DIN 1946-6:2019-12`:
  <https://www.din.de/de/service-fuer-anwender/normungsportale/normungsportal-klima-und-lueftungstechnik/faqs-zur-din-1946-6-2019-12-901724>
- BMUKN/UBA zu richtigem Lueften und Schimmelvermeidung:
  <https://www.bundesumweltministerium.de/themen/gesundheit/innenraumluft/richtiges-lueften-und-heizen>
- US EPA zu Wohnraumlueftung und ASHRAE 62.2:
  <https://www.epa.gov/indoor-air-quality-iaq/how-much-ventilation-do-i-need-my-home-improve-indoor-air-quality>
- ASHRAE 62.2 Uebersicht:
  <https://www.ashrae.org/technical-resources/bookstore/standards-62-1-62-2>
- Analog Devices TMC2209-Datenblatt:
  <https://www.analog.com/media/en/technical-documentation/data-sheets/tmc2209_datasheet_rev1.09.pdf>

## RS485, Modbus und Loxone

Primaere Home-Automation-Schnittstelle ist Modbus RTU auf `Serial1`.

| Parameter | Wert |
| --- | --- |
| Rolle | Slave |
| Default-ID | `1` |
| Baudrate | `38400` |
| Datenformat | `8N1` |
| Modbus-Funktionen | `0x03`, `0x06`, `0x10` |
| Broadcast-ID `0` | wird ignoriert, keine Antwort |
| Fremde ID | wird ignoriert, keine Antwort |
| Fehlerhafte CRC | wird ignoriert |

### Holding-Register

Adressen sind 0-basiert, passend fuer Loxone Config.

| Adresse | Zugriff | Bedeutung |
| --- | --- | --- |
| 0 | R/W | Kommando: `0` none, `1` home, `2` reset, `3` soft endstops on, `4` off, `5` refresh machine |
| 1 | R/W | Zielposition Steps High Word |
| 2 | R/W | Zielposition Steps Low Word |
| 3 | R/W | Soft-Min Steps High Word |
| 4 | R/W | Soft-Min Steps Low Word |
| 5 | R/W | Soft-Max Steps High Word |
| 6 | R/W | Soft-Max Steps Low Word |
| 7 | R/W | Modbus-/Geraete-ID `1..247` |
| 8 | R | Controller-State |
| 9 | R | Flags: Bit0 ready, Bit1 fault, Bit2 soft endstops active |
| 10 | R | Istposition Steps High Word |
| 11 | R | Istposition Steps Low Word |
| 12 | R | Homing-Max Steps High Word |
| 13 | R | Homing-Max Steps Low Word |
| 14 | R/W | Zielposition `0..1000` Promille vom Homing-Weg |
| 15 | R | Istposition `0..1000` Promille vom Homing-Weg |
| 16 | R/W | Safe-Position `0..1000` Promille; wird im Flash gespeichert |

Empfohlen fuer Loxone:

- Register `14` als analoger Aktor `0..1000`.
- Register `16` als Parameter fuer die Safe-Position verwenden.
- Register `8`, `9`, `15`, `16` langsam pollen, z. B. alle `2..5 s`.
- Register `0` mit Wert `5` als Fehler-Rehome/Refresh-Machine-Befehl
  vorsehen, damit nach Blockade oder Endschalterfehler kein Pico-Reset noetig
  ist.
- Register `0..16` koennen in einem Holding-Register-Block gelesen werden,
  damit Loxone/Modbus-TCP-Gateways den Zustand konsistent erfassen.
- Schnelle Bewegung, Endschalter, Softlimits, StallGuard und Fehlerbehandlung
  lokal im Controller lassen.

## Service-Textprotokoll

Das alte Textprotokoll bleibt als Service-/Debugpfad erhalten. Auf RS485 werden
nur adressierte Textbefehle angenommen, damit mehrere Slaves am Bus nicht
kollidieren.
Auf USB-Serial `115200` werden dieselben Befehle auch ohne `ID<n>` akzeptiert;
adressierte Befehle funktionieren dort ebenfalls.

| Befehl | Wirkung |
| --- | --- |
| `ID<n> GOTO <steps>` | Zielposition in Steps setzen |
| `ID<n> POS?` | aktuelle Position melden |
| `ID<n> HOME` | Homing starten |
| `ID<n> REFRESH` | Fehler quittieren und Homing ohne MCU-Reset starten |
| `ID<n> REFRESH MACHINE` | Alias fuer `REFRESH` |
| `ID<n> RESET` | Kompatibilitaetsbefehl fuer Fehler quittieren und Homing |
| `ID<n> SOFTMIN <steps>` | Soft-Min setzen |
| `ID<n> SOFTMAX <steps>` | Soft-Max setzen |
| `ID<n> SOFTENDSTOPS ON` | Soft-Endstops aktivieren |
| `ID<n> SOFTENDSTOPS OFF` | Soft-Endstops deaktivieren |
| `ID<n> SOFTENDSTOPS?` | Soft-Endstop-Status melden |
| `ID<n> ID?` | Geraete-ID melden |
| `ID<n> ID <1..247>` | Geraete-ID setzen |
| `ID<n> SETID <1..247>` | Alias fuer ID setzen |
| `ID<n> SAFE?` | Safe-Position in Promille melden |
| `ID<n> SAFE <0..1000>` | Safe-Position setzen und speichern |

Debug-/Eventtexte gehen standardmaessig auf USB `Serial`, nicht auf RS485.
Damit stoeren Homing-, Fehler- oder TMC-Meldungen keine Modbus-Kommunikation.
Nur fuer Servicebetrieb kann `LUEFTERKLAPPE_EVENTS_TO_RS485=1` gesetzt werden.

## How To

### 1. Abhaengigkeiten installieren

PlatformIO wird fuer Firmware und Tests benoetigt. Mermaid CLI ist als lokale
Dev-Dependency installiert und wird ueber `npm install` wiederhergestellt.

```powershell
& 'C:\Users\mosei\AppData\Local\Programs\Python\Python312\python.exe' -m pip install --upgrade platformio
$env:PLATFORMIO_CORE_DIR=Join-Path $env:USERPROFILE '.platformio-luefter'
$env:PATH='C:\msys64\mingw64\bin;' + $env:PATH
& C:\msys64\mingw64\bin\npm.cmd install
```

### 2. Firmware bauen

```powershell
$env:PLATFORMIO_CORE_DIR=Join-Path $env:USERPROFILE '.platformio-luefter'
platformio run -e pico
```

### 3. Native Tests ausfuehren

```powershell
$env:PLATFORMIO_CORE_DIR=Join-Path $env:USERPROFILE '.platformio-luefter'
platformio test -e native
```

### 4. Quality Checks ausfuehren

```powershell
$env:PLATFORMIO_CORE_DIR=Join-Path $env:USERPROFILE '.platformio-luefter'
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_checks.ps1
```

Das Skript fuehrt native Tests, Pico-Build, clang-tidy, cppcheck und den
MISRA-Addon-Pfad aus.

### 5. Geraete-ID setzen

Default-ID beim Build setzen:

```ini
build_flags =
  ${env.build_flags}
  -DLUEFTERKLAPPE_DEFAULT_DEVICE_ID=7
```

Oder zur Laufzeit ueber Modbus Holding-Register `7` bzw. Servicebefehl:

```text
ID1 SETID 7
```

Die Laufzeit-ID wird zusammen mit der Safe-Position im letzten Pico-Flash-
Sektor gespeichert. Der Datensatz enthaelt Magic, Version und CRC; bei
ungueltigem oder leerem Speicher werden Defaultwerte benutzt.

Safe-Position setzen:

```text
ID1 SAFE 1000
```

Alternativ Modbus Holding-Register `16` mit `0..1000` schreiben.

### 6. Loxone konfigurieren

1. Im Configurator `Export testen` ausfuehren und die Dateien
   `MB_Luefterklappe_FanFlap_ID<id>.xml`, `loxone-fanflap-<id>.json` und
   `loxone-fanflap-<id>.md` herunterladen.
2. Loxone Modbus Extension als RTU-Master verwenden oder den lokalen
   Modbus-TCP-Gateway des Configurators auf `127.0.0.1:5020` nutzen.
3. Die `MB_*.xml` in den Loxone-Config-Template-Ordner `Comm` importieren
   bzw. kopieren.
4. Schnittstelle auf `38400`, `8N1` einstellen.
5. Slave-ID auf die Firmware-ID setzen, Default `1`.
6. Analogausgang auf Holding-Register `14`, Wertebereich `0..1000`.
7. Parameter fuer Safe-Position auf Holding-Register `16`, Wertebereich
   `0..1000`.
8. Status langsam lesen: Register `8`, `9`, `15`, `16`, Intervall `2..5 s`.
9. Bei mehreren Klappen jede Klappe mit eigener ID betreiben.

Manuelle Minimal-Konfiguration ohne Exportdateien:

1. Loxone Modbus Extension als RTU-Master verwenden.
2. Schnittstelle auf `38400`, `8N1` einstellen.
3. Slave-ID auf die Firmware-ID setzen, Default `1`.
4. Analogausgang auf Holding-Register `14`, Wertebereich `0..1000`.
5. Parameter fuer Safe-Position auf Holding-Register `16`, Wertebereich
   `0..1000`.
6. Status langsam lesen: Register `8`, `9`, `15`, `16`, Intervall `2..5 s`.
7. Bei mehreren Klappen jede Klappe mit eigener ID betreiben.

### 7. Logic-Analyzer-Test ausfuehren

sigrok-cli ist fuer Hardware-Abnahme vorgesehen. Der aktuelle 8-Kanal-Default
passt zu `fx2lafw`/Saleae-kompatiblen Analyzern.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\la\capture_luefterklappe.ps1 `
  -Driver fx2lafw `
  -SampleRate 4000000 `
  -SampleRateHz 4000000 `
  -Duration 20s `
  -ExpectedId 1
```

Ohne Hardware nur Skript und Analyzer pruefen:

```powershell
python .\tools\la\analyze_la_capture.py --self-test
powershell -ExecutionPolicy Bypass -File .\tools\la\capture_luefterklappe.ps1 -PrintOnly
```

Details zu Testmatrix und Akzeptanzkriterien stehen in `tools/la/README.md`.

### 8. Mermaid-Diagramme rendern

GitHub rendert Mermaid-Codebloecke im README direkt. SVG-Dateien koennen lokal
mit Mermaid CLI erzeugt werden:

```powershell
$env:PATH='C:\msys64\mingw64\bin;' + $env:PATH
& C:\msys64\mingw64\bin\npm.cmd run diagrams
```

Quellen:

- `docs/diagrams/architecture.mmd`
- `docs/diagrams/wiring.mmd`
- `docs/diagrams/puppeteer.json`

## Tests und Checks

```powershell
$env:PLATFORMIO_CORE_DIR=Join-Path $env:USERPROFILE '.platformio-luefter'
platformio test -e native
platformio run -e pico
platformio check -e native --skip-packages
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_checks.ps1
python .\tools\la\analyze_la_capture.py --self-test
powershell -ExecutionPolicy Bypass -File .\tools\la\capture_luefterklappe.ps1 -PrintOnly
```

Der native Testlauf deckt Homing-Timing, `millis()`-Wraparound,
Reset-Timeout, unerwartete Endschalter, StallGuard-Redundanz beim Homing,
Wegfahr-Free-Check, Safe-Position, Flash-Persistenz mit CRC,
Soft-Endstop-Clamping, ungueltige serielle Argumente, ID-Adressierung,
Modbus-RTU-CRC, Fremdadressen, Exception Responses, Promille-Zielregister,
Service-Bewegungen ohne Soft-Endstops und TMC2209-UART-Frames ab.

## MCU-Portierung

Ein neuer MCU braucht nur Adapter fuer diese Interfaces:

- `StepperPort`: Enable, Geschwindigkeit/Beschleunigung, Zielposition, Stop,
  Run, aktuelle Position und aktuelle Geschwindigkeit.
- `UartPort`: Byte schreiben, Flush, verfuegbare Bytes, Byte lesen.
- `DelayPort`: blockierende Millisekunden-Wartezeit fuer TMC-Initialisierung.
- `EventSink`: Ausgabe oder Logging der Controller-Ereignisse.

Minimaler Ablauf:

1. `FanFlapController controller(stepperPort, eventSink);`
2. `Tmc2209Driver tmc(uartPort, delayPort, eventSink);`
3. `ModbusRtuServer modbus(controller, rs485UartPort);`
4. Im Setup `controller.begin()` und `tmc.initialize()` aufrufen.
5. In der Hauptschleife serielle Bytes an `modbus.handleByte(...)` geben und
   zyklisch `controller.tick(inputs, nowMs)` aufrufen.
