# Logic-Analyzer-Tests mit sigrok-cli

Diese Tests pruefen die 80-mm-Rohrventil-Steuerung nicht nur auf
Quellcode-Ebene, sondern am realen Signal: RS485/Modbus, TMC-UART und
Stepper-Timing werden aus einem sigrok-Capture dekodiert und offline validiert.

## Voraussetzungen

- `sigrok-cli` im PATH oder in `%LOCALAPPDATA%\sigrok\sigrok-cli`
- Logic Analyzer mit sigrok-Treiber, z. B. `fx2lafw`
- Python 3 fuer `analyze_la_capture.py`
- Gemeinsame Masse zwischen Pico, RS485-Modul, TMC-Treiber und Analyzer

Kanonische Wiring-Quelle ist `README.md` zusammen mit
`docs/diagrams/wiring.mmd`. Der aktuelle Aufbau nutzt CH0 des
Waveshare-Pico-2CH-RS485 fuer Modbus RTU. CH1 bleibt elektrisch als
Durchschleif-/Reservekanal frei und wird in den Standard-Captures nicht
dekodiert.

## Verkabelung

| LA | Signal | Pico / Funktion | Erwartung |
| --- | --- | --- | --- |
| D0 | RS485 device TX | GP0 / `Serial1 TX` zum DI des RS485-Moduls | Modbus-Antworten |
| D1 | RS485 device RX | GP1 / `Serial1 RX` vom RO des RS485-Moduls | Modbus-Anfragen |
| D2 | STEP | Stepper STEP | Pulsbreite und Pulsfolge |
| D3 | DIR | Stepper DIR | Setup/Hold um STEP |
| D4 | MIN switch | Endschalter min | Rare-case Diagnose |
| D5 | MAX switch | Endschalter max | Rare-case Diagnose |
| D6 | ENABLE | Stepper Enable, aktiv low | Keine STEP-Pulse im Disable |
| D7 | TMC TX | GP8 / Pico -> TMC UART | TMC-Write/Read-Requests |
| D8 | TMC RX | GP9 / TMC -> Pico UART | Optional bei 9+ Kanaelen |

RS485 A/B nicht direkt mit einem 3.3-V-Logic-Analyzer messen. Fuer die
UART-Dekodierung an GP0/GP1 bzw. DI/RO des Transceivers messen.

Der Standardlauf ist auf 8-Kanal-Analyzer wie `fx2lafw` ausgelegt und dekodiert
TMC-TX. Fuer TMC-RX entweder einen 9+/16-Kanal-Analyzer nutzen:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\la\capture_luefterklappe.ps1 `
  -Channels D0,D1,D2,D3,D4,D5,D6,D7,D8 `
  -TmcRxChannel D8
```

Oder einen zweiten 8-Kanal-Lauf machen und D7 von GP8 auf GP9 umstecken:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\la\capture_luefterklappe.ps1 `
  -TmcTxChannel off `
  -TmcRxChannel D7
```

## Schnellstart

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\la\capture_luefterklappe.ps1 `
  -Driver fx2lafw `
  -SampleRate 4000000 `
  -SampleRateHz 4000000 `
  -Duration 20s `
  -ExpectedId 1
```

Das Skript legt pro Lauf `artifacts/la/<timestamp>/` an:

- `capture.sr`: sigrok-Session
- `rs485_device_rx.bin`: Modbus-Anfragen an die Steuerung
- `rs485_device_tx.bin`: Modbus-Antworten der Steuerung
- `tmc_tx.bin`: TMC-UART vom Pico zum Treiber
- `tmc_rx.bin`: TMC-UART vom Treiber zum Pico
- `samples.csv`: digitale Rohsamples fuer STEP/DIR/ENABLE-Pruefungen

Zum Testen ohne Hardware:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\la\capture_luefterklappe.ps1 -PrintOnly
python .\tools\la\analyze_la_capture.py --self-test
```

Firmware-Release-Abnahme bindet diesen Pfad optional ein:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\firmware_release_check.ps1 -SerialPort COMx -ExpectedDeviceId 1 -RequireLogicAnalyzer
```

Ohne `-RequireLogicAnalyzer` prueft der Release-Check weiterhin Hard-Gate,
UF2-Artefakt, SHA256, Service-Textdiagnose und Modbus-Diagnoseregister. Mit
`-RequireLogicAnalyzer` muss zusaetzlich `capture_luefterklappe.ps1` ohne
`FAIL:`-Zeile durchlaufen.

## Harte Testmatrix

1. **Boot und Homing**
   - Capture: 20 s ab Reset.
   - Erwartung: keine ungueltigen Modbus-Frames, TMC-UART mit gueltiger CRC,
     keine STEP-Pulse solange ENABLE disabled ist.

2. **Loxone/Modbus Idle Polling**
   - Master liest Register `8..15` zyklisch bei ID `1`.
   - Erwartung: jede Antwort nutzt ID `1`, CRC ist korrekt, keine Broadcast-
     Antwort, keine ASCII-Events auf RS485.

3. **Positionsbefehle**
   - Register `14` nacheinander auf `0`, `500`, `1000`, `500`, `0` schreiben.
   - Erwartung: Function `0x06`/`0x10` wird quittiert, STEP-Pulse treten nur
     waehrend Bewegung auf, DIR wechselt nicht direkt am STEP-Rising-Edge.

4. **Adressierung und Bus-Sicherheit**
   - Anfrage an falsche ID, Broadcast-ID `0`, kaputte CRC, unbekannte Funktion.
   - Erwartung: falsche ID und Broadcast erzeugen keine Antwort; ungueltige
     Register/Funktionen erzeugen Modbus-Exception-Frames mit korrekter CRC.

5. **Frame-Timeout und Resync**
   - Halbe Modbus-Frames senden, Pause > RTU-Frame-Timeout, danach gueltige
     Anfrage.
   - Erwartung: Parser verwirft den halben Frame und beantwortet den naechsten
     gueltigen Frame korrekt.

6. **Bewegung unter Last**
   - Waehrend einer langen Fahrt Statusregister pollen.
   - Erwartung: Modbus bleibt antwortfaehig, STEP-Timing verletzt keine
     Mindestwerte, keine Response-Fragmente.

7. **Endschalter und Rare Cases**
   - MIN/MAX waehrend Fahrt ausloesen, Prellen provozieren, Reset-Befehl senden.
   - Erwartung: keine STEP-Pulse nach Stop/Fault, Status/Flags wechseln
     nachvollziehbar, anschliessender Reset/Homing ist wieder dekodierbar.

8. **TMC-UART Stoerung**
   - TMC RX kurz trennen oder Treiber nicht versorgen.
   - Erwartung: TMC-TX bleibt gueltig dekodierbar; fehlende RX-Frames werden als
     Diagnose sichtbar, nicht als falsche gueltige Responses.

9. **Release-Diagnose und Konfigurationsregister**
   - Master liest Register `0..27`, danach `17..22` und optional `23..27`.
   - Erwartung: gueltige CRC-Frames, read-only Diagnosebereich, Schreibversuche
     auf `17..22` liefern Illegal-Address-Exceptions; Grad- und
     StallGuard-Register bleiben innerhalb ihrer Wertebereiche.

## Offline-Analyse

Ein vorhandener Capture kann ohne neue Messung erneut ausgewertet werden:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\la\capture_luefterklappe.ps1 `
  -SkipCapture `
  -InputCapture .\artifacts\la\20260522-120000\capture.sr `
  -ExpectedId 1
```

Nur der Analyzer auf bereits exportierten Dateien:

```powershell
python .\tools\la\analyze_la_capture.py `
  --expected-id 1 `
  --rs485-device-rx-bin .\artifacts\la\<run>\rs485_device_rx.bin `
  --rs485-device-tx-bin .\artifacts\la\<run>\rs485_device_tx.bin `
  --tmc-tx-bin .\artifacts\la\<run>\tmc_tx.bin `
  --tmc-rx-bin .\artifacts\la\<run>\tmc_rx.bin `
  --csv .\artifacts\la\<run>\samples.csv `
  --samplerate-hz 4000000 `
  --enable-active-low
```

## Akzeptanzkriterien

- `analyze_la_capture.py` meldet keine `FAIL:` Zeilen.
- RS485-Antworten haben immer die erwartete Geraete-ID.
- Broadcast-Adresse `0` erzeugt keine Antwort.
- CRC-Fehler werden nicht als gueltige Frames akzeptiert.
- TMC-UART-Frames haben gueltige CRC, 4-Byte-Requests und 8-Byte-Responses
  werden sauber resynchronisiert.
- STEP-High-Pulse sind mindestens `1 us` breit.
- ENABLE disabled bedeutet: keine STEP-Rising-Edges.
- DIR setup/hold um STEP-Rising-Edges ist mindestens `2 us`.

Falls dein sigrok-Treiber andere Kanalnamen verwendet, die `D0..D8`-Parameter
im Skript passend ueberschreiben.
