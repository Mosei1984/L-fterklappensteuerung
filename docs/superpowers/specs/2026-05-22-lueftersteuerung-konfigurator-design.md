# Lueftersteuerung Konfigurator Design

Datum: 2026-05-22

## Ziel

Der erste Konfigurator ist ein lokales C# PC-Service-Tool fuer die
Luefterklappensteuerung eines 80-mm-Rohrventils. Er soll den angeschlossenen
Controller finden, konfigurieren, testen und Integrationsdateien fuer
Home-Automation-Systeme erzeugen.

Die Architektur muss spaeter weitere Controller-Typen tragen. Die
Luefterklappe ist deshalb ein erstes Geraeteprofil, nicht fest in die App
eingebacken.

## Nicht-Ziele fuer Release 1

- Keine Cloud-Anbindung.
- Keine dauerhafte Hausautomations-Bridge fuer alle Systeme gleichzeitig.
  Release 1 enthaelt nur ein lokal explizit gestartetes Modbus-TCP-Gateway.
- Keine automatische Aenderung fremder Home-Automation-Installationen ohne
  explizite Benutzerauswahl.
- Keine Veraenderung des MCU-Cores als Voraussetzung fuer den ersten
  Konfigurator. Spaetere Firmware-Metadatenbefehle sind optional.

## Release-1-Funktionen

- Lokaler ASP.NET-Core-Service mit Browser-UI auf `localhost`.
- Serielle Verbindung zur Pico-Firmware ueber USB CDC Textprotokoll.
- Modbus-RTU-Testpfad ueber USB-RS485-Adapter.
- Mehrere Controller gleichzeitig erkennen und getrennt verwalten.
- Controller-Erkennung mit Port, Transport, ID, Status, Position,
  Safe-Position und Fehlerstatus.
- Konfiguration von:
  - Geraete-ID `1..247`
  - Safe-Position `0..1000`
  - Baudrate/Modbus-Parameter, soweit Firmware und Profil sie unterstuetzen
  - Endstop- und StallGuard-bezogene Diagnose-/Pruefwerte, soweit verfuegbar
- Live-Diagnose fuer Status, Fehler, Position, Homing-Zustand und
  Kommunikationsqualitaet.
- Gefuehrte Tests:
  - Textprotokoll-Ping
  - Modbus-RTU-Read
  - Modbus-RTU-Write mit Bestaetigung
  - Safe-Position pruefen
  - Exportierte Integration gegen Profil validieren
- Export-Adapter fuer:
  - Loxone Modbus-Registerliste und Einrichtungsnotizen
  - Home Assistant MQTT-Discovery-JSON/YAML
  - openHAB Thing-/Item-Vorlagen
  - generische Modbus-TCP/RTU-Registerdokumentation
- MQTT-Live-Test gegen einen Broker, wenn Brokerdaten angegeben werden.
- Modbus-TCP-Gateway fuer ausgewaehlte lokale Controller. Das Gateway ist in
  Release 1 ein bewusst gestarteter Service-Modus, kein automatisch laufender
  Hintergrunddienst.
- Firmware-Update per UF2:
  - lokale UF2-Datei auswaehlen
  - Pico-Bootloader-Laufwerk erkennen
  - optionalen Bootloader-Reset ueber Serial versuchen, falls Board/Firmware
    das unterstuetzt
  - UF2-Datei kopieren und nach Neustart Verbindung/Settings pruefen
- Controller-Profilimport aus JSON mit Schema-Validierung.
- Loxone Setup Wizard als primaerer Nutzerpfad.
- Sichtbares App-Logo/Icon fuer Browser, EXE, Startmenue und Windows-Paket.
- Windows-Paket mit EULA, Installationsstatus und Deinstallationsskript.

## Architektur

Der Konfigurator besteht aus einem lokalen Service, einer Browser-UI und klaren
Modulen fuer Controller-Profile, Transporte, Protokolle und
Home-Automation-Adapter.

```text
Browser UI
  -> Local HTTP API / SignalR
    -> Application Services
      -> Controller Profile Registry
      -> Transport Registry
      -> Protocol Registry
      -> Integration Adapter Registry
      -> Gateway Registry
      -> Local Configuration Store
        -> USB CDC / RS485 / MQTT / Modbus TCP / UF2 Filesystem
```

### Vorgeschlagene Dateistruktur

```text
tools/configurator/
  LuefterConfigurator.sln
  src/
    LuefterConfigurator.Host/
      Program.cs
      appsettings.json
    LuefterConfigurator.Domain/
      ControllerProfile.cs
      ControllerState.cs
      SettingDefinition.cs
      RegisterDefinition.cs
      AdapterContracts.cs
    LuefterConfigurator.Application/
      DeviceDiscoveryService.cs
      ConfigurationService.cs
      DiagnosticsService.cs
      ExportService.cs
      FirmwareUpdateService.cs
      MultiControllerSessionService.cs
      ProfileImportService.cs
      ModbusTcpGatewayService.cs
    LuefterConfigurator.Infrastructure.Serial/
      UsbSerialTransport.cs
      ModbusRtuSerialTransport.cs
    LuefterConfigurator.Infrastructure.ModbusTcp/
      ModbusTcpGateway.cs
    LuefterConfigurator.Infrastructure.Mqtt/
      MqttLiveTestAdapter.cs
    LuefterConfigurator.Infrastructure.Firmware/
      PicoUf2FirmwareUpdater.cs
      Uf2DriveDetector.cs
    LuefterConfigurator.Adapters.Loxone/
      LoxoneExportAdapter.cs
    LuefterConfigurator.Adapters.HomeAssistant/
      HomeAssistantMqttDiscoveryAdapter.cs
    LuefterConfigurator.Adapters.OpenHab/
      OpenHabExportAdapter.cs
    LuefterConfigurator.Adapters.Modbus/
      ModbusRegisterExportAdapter.cs
    LuefterConfigurator.Profiles.FanFlap/
      FanFlapProfile.cs
      FanFlapTextProtocol.cs
      FanFlapModbusProtocol.cs
      FanFlapModbusMap.cs
    LuefterConfigurator.Profiles.Json/
      JsonProfileImporter.cs
      controller-profile.schema.json
  tests/
    LuefterConfigurator.Tests/
```

Die Struktur ist bewusst grobmodular. Jeder Adapter hat eigene Dateien und
eigene Tests. Die Host-App kennt Adapter nur ueber Interfaces.

## Kerninterfaces

### Controller-Profile

Ein `ControllerProfile` beschreibt, was ein Geraet kann:

- Profil-ID, Anzeigename, Firmware-Kompatibilitaet.
- Unterstuetzte Transporte: USB Text, Modbus RTU, Modbus TCP Gateway.
- Registerdefinitionen mit Adresse, Typ, Einheit, Grenzwerten und Zugriff.
- Einstellungen mit Validierungsregeln.
- Diagnosefelder und Fehlercodes.
- Exportfaehigkeiten fuer Home-Automation-Systeme.

Release 1 enthaelt `FanFlapProfile` und einen JSON-Profilimport. Importierte
Profile sind reine Daten: kein Skriptcode, keine dynamischen Assemblies, keine
externen Prozessaufrufe. Der Import akzeptiert nur Profile, die gegen
`controller-profile.schema.json` validieren.

### Transporte

`IControllerTransport` kapselt die physische Verbindung:

- Ports auflisten.
- Verbindung oeffnen/schliessen.
- Request/Response senden.
- Timeout, Retry und Fehlerklassifikation liefern.

Release 1:

- `UsbSerialTransport` fuer USB CDC und Textprotokoll.
- `ModbusRtuSerialTransport` fuer USB-RS485-Adapter.
- Sitzungsverwaltung fuer mehrere gleichzeitige Controller.

Spaeter:

- Weitere Netzwerktransporte ausserhalb des lokalen Modbus-TCP-Gateways.

### Protokolle

`IControllerProtocol` uebersetzt fachliche Operationen in konkrete Befehle:

- `ReadIdentity`
- `ReadStatus`
- `ReadSettings`
- `WriteSetting`
- `RunDiagnostic`
- `ResetFault`
- `RefreshMachine`

Release 1:

- `FanFlapTextProtocol` als primaerer Servicepfad.
- `FanFlapModbusProtocol` fuer Modbus-RTU-Test und Integrationsvalidierung.

### Home-Automation-Adapter

`IHomeAutomationAdapter` kapselt Integrationen:

- Faehigkeiten melden: Export, Live-Test, Bridge-faehig.
- Profil gegen Adapterregeln validieren.
- Integrationsdateien erzeugen.
- Optional Live-Verbindung testen.

Release 1:

- `LoxoneExportAdapter`.
- `HomeAssistantMqttDiscoveryAdapter`.
- `MqttLiveTestAdapter`.
- `OpenHabExportAdapter`.
- `ModbusRegisterExportAdapter`.

Die Adapter werden von Anfang an als echte Module angelegt. Live-Betrieb wird
aber nur dort aktiviert, wo er im ersten Schritt risikoarm testbar ist.

### Modbus-TCP-Gateway

Das Modbus-TCP-Gateway stellt ausgewaehlte lokal verbundene Controller als
Modbus-TCP-Server bereit. Es ist ein Gateway-Modul, kein Ersatz fuer die
Controller-Firmware.

Release-1-Regeln:

- Gateway bindet standardmaessig nur an `127.0.0.1`.
- Bindung an LAN-IP ist eine explizite Benutzerauswahl.
- Jeder gemappte Controller bekommt eine eindeutige Unit-ID.
- Kollisionen zwischen Controller-ID, Modbus-RTU-ID und TCP-Unit-ID werden vor
  Start blockiert.
- Schreibzugriffe koennen global deaktiviert werden.
- Jede Gateway-Antwort wird aus dem aktiven Profil/Registermodell erzeugt.

### Firmware-Update

UF2-Flashing ist ein eigenes Modul mit klarer Bedienfuehrung:

- UF2-Datei lokal auswaehlen.
- Optionalen Bootloader-Reset ueber Serial versuchen.
- Wenn kein Bootloader-Laufwerk erscheint, Benutzer zum manuellen BOOTSEL-Start
  fuehren.
- UF2-Datei auf das erkannte Laufwerk kopieren.
- Nach Neustart Port neu suchen, Controller identifizieren und Settings
  pruefen.

Das Tool flasht nur UF2-Dateien, die der Benutzer explizit auswaehlt. Ein
Firmware-Paket kann spaeter Metadaten enthalten; Release 1 prueft mindestens
Dateiendung, Lesbarkeit, Zielprofil und Benutzerbestaetigung.

## UI-Konzept

Die UI ist eine lokale Arbeitsoberflaeche, keine Landingpage. Release 1
priorisiert den Loxone Setup Wizard fuer das 80-mm-Rohrventil; Host- und
Rohprotokolltests bleiben als Expertentest getrennt.

Hauptbereiche:

- `Loxone Setup`: gefuehrter Wizard von Loxone-Vorbereitung ueber USB-Erkennung,
  UF2-Flashing, ID/Safe-Write, Exportdownload und Abschlusscheck.
- `Verbinden`: COM-Port, Baudrate, Transportwahl, Scan, Verbindungstest.
- `Geraete`: Liste aller erkannten Controller, Port, Transport, ID,
  Firmware/Profil, Konflikte.
- `Status`: ID, Position, Ziel, State, Fehler, Safe-Position,
  Kommunikationszaehler fuer den ausgewaehlten Controller.
- `Konfiguration`: editierbare Settings mit Grenzwertvalidierung und
  Schreibbestaetigung.
- `Diagnose`: Ping, Modbus-Read, Modbus-Write-Test, Safe-Position-Test,
  Refresh Machine/Fehler-Rehome, kompatibler Fehler-Reset, Log.
- `Integrationen`: Loxone, Home Assistant, MQTT, Modbus, openHAB Exporte und
  Live-Tests.
- `Gateway`: Modbus-TCP-Gateway starten/stoppen, Unit-IDs zuordnen,
  Schreibzugriff freigeben/sperren.
- `Firmware`: UF2-Datei waehlen, Bootloader erkennen, Flash-Ablauf verfolgen,
  Verbindung nach Update pruefen.
- `Profile`: sichtbare Profil- und Firmware-Kompatibilitaet, JSON-Profilimport.

Riskante Aktionen verwenden eine explizite Bestaetigung und zeigen vor dem
Schreiben alte und neue Werte.

## Datenfluss

1. Benutzer startet den lokalen Service.
2. Browser-UI oeffnet `http://localhost:<port>`.
3. UI fordert Portliste an.
4. Benutzer verbindet einen oder mehrere Controller ueber USB CDC oder RS485.
5. Service legt pro Controller eine eigene Session an.
6. Service erkennt Profil und liest Status/Settings je Session.
7. Benutzer waehlt einen Controller aus und aendert Settings.
8. Application-Service validiert gegen Profilgrenzen.
9. Protocol-Service schreibt Werte in die passende Session.
10. Service liest Werte erneut und zeigt nur bestaetigte Werte als gespeichert.
11. Export-Service erzeugt Integrationsdateien aus Profil, Settings und
    Adapterregeln.
12. Optional startet der Benutzer das Modbus-TCP-Gateway fuer ausgewaehlte
    Controller.

## Sicherheit und Fehlermodell

- Kein Schreibbefehl ohne erfolgreiche Geraeteidentifikation.
- Geraete-ID wird auf `1..247` begrenzt.
- Safe-Position wird auf `0..1000` begrenzt.
- Bei mehreren Controllern werden Schreibbefehle immer an eine explizite
  Controller-Session gebunden.
- ID-Kollisionen werden sichtbar markiert und blockieren Gateway-Start.
- Schreibbefehle werden per Readback verifiziert.
- Fehler-Recovery nutzt `RefreshMachine` zuerst: Controllerfehler quittieren
  und Homing neu starten, ohne den Pico/MCU zu rebooten.
- UF2-Flashing verlangt Benutzerbestaetigung, ein erkanntes Bootloader-Laufwerk
  und eine erfolgreiche Wiedererkennung nach Neustart.
- JSON-Profilimport ist strikt datenbasiert und fuehrt keinen Code aus.
- Kommunikationsfehler werden klassifiziert:
  - Port nicht vorhanden
  - Port belegt
  - Timeout
  - CRC/Modbus-Fehler
  - falsche ID
  - unerwartete Antwort
  - Firmware/Profil inkompatibel
  - Gateway-Port belegt
  - doppelte Unit-ID
  - UF2-Laufwerk nicht gefunden
- UI unterscheidet zwischen Diagnosefehler und Controller-Fehlerzustand.
- Exportdateien werden vor Ausgabe gegen das aktive Profil validiert.
- MQTT-Live-Test schreibt nur Testtopics, solange keine Bridge aktiv ist.
- Lokaler Service bindet standardmaessig nur an `127.0.0.1`.
- Modbus-TCP-Gateway bindet standardmaessig nur an `127.0.0.1`.

## Teststrategie

### Unit-Tests

- Profilvalidierung: Grenzwerte, Registertypen, Pflichtfelder.
- Textprotokoll-Parser: gueltige Antworten, Timeouts, kaputte Antworten.
- Modbus: CRC, Exception Responses, Register-Mapping.
- Settings-Validierung: ID, Safe-Position, Baudrate.
- Multi-Controller-Sessions: getrennte Ports, getrennte IDs, Kollisionen,
  parallele Reads ohne vermischte Antworten.
- Modbus-TCP-Gateway: Unit-ID-Mapping, Read/Write, Exception Responses,
  deaktivierter Schreibzugriff.
- UF2-Updater: Laufwerkserkennung, Dateivalidierung, Fehler bei fehlendem
  Laufwerk, erfolgreicher Kopierpfad mit Temp-Verzeichnis.
- JSON-Profilimport: gueltiges Profil, falsche Schema-Version, doppelte
  Register, ungueltige Grenzwerte, nicht erlaubte Felder.
- Adapter-Exports:
  - Loxone Registerliste enthaelt korrekte Adressen und Datentypen.
  - Home Assistant Discovery enthaelt eindeutige Entity IDs und Topics.
  - openHAB Templates referenzieren korrekte Channels.
  - Modbus-Doku stimmt mit Profil-Registermap ueberein.

### Integrationstests ohne Hardware

- Fake-Serial-Port fuer Textprotokoll.
- Fake-Modbus-Device fuer RTU-Reads/Writes.
- Zwei oder mehr Fake-Controller fuer Session- und ID-Konflikttests.
- Fake-Modbus-TCP-Client gegen lokalen Gateway-Server.
- Temp-Laufwerk/Temp-Verzeichnis fuer UF2-Updatepfad.
- Fake-MQTT-Broker oder testbarer MQTT-Client-Stub.
- Export-Snapshot-Tests fuer stabile Integrationsdateien.

### Hardware-nahe Tests

- Pico per USB CDC:
  - `ID?`, `POS?`, `SAFE?`, Status lesen.
  - ID schreiben und Readback pruefen.
  - Safe-Position schreiben und Readback pruefen.
- USB-RS485-Adapter:
  - Modbus Read Holding Registers.
  - Modbus Write Single Register fuer erlaubte Settings.
  - falsche ID und CRC-Fehler pruefen.
- Mehrere Controller:
  - Zwei COM-Ports parallel oeffnen.
  - Getrennte Statusreads pruefen.
  - ID-Kollision erkennen und Gateway-Start blockieren.
- UF2:
  - Bootloader-Laufwerk erkennen.
  - UF2 kopieren.
  - Controller nach Neustart erneut finden.

## Abnahmekriterien fuer Release 1

- Der Service startet lokal ohne Adminrechte.
- Die Browser-UI erkennt mindestens einen COM-Port und kann mehrere Controller
  als getrennte Sessions anzeigen.
- Ein angeschlossener Pico kann ueber USB CDC gelesen werden.
- ID und Safe-Position koennen geaendert und per Readback bestaetigt werden.
- Modbus-RTU-Kommunikation kann ueber USB-RS485 getestet werden.
- Modbus-TCP-Gateway kann fuer ausgewaehlte Controller lokal gestartet und
  wieder gestoppt werden.
- UF2-Flashing kann ueber ein erkanntes Pico-Bootloader-Laufwerk durchgefuehrt
  und danach per Wiedererkennung validiert werden.
- JSON-Profile koennen importiert, validiert und in der Profilregistry
  angezeigt werden.
- Loxone-, Home-Assistant-, MQTT-, Modbus- und openHAB-Exports werden aus dem
  aktiven Profil erzeugt.
- Alle Adapter sind ueber Registry/Interface angebunden, nicht direkt in der UI
  verdrahtet.
- Unit- und Integrationstests laufen ueber einen dokumentierten Befehl.
- Bestehende Firmware-Tests bleiben unveraendert gruen.

## Offene spaetere Erweiterungen

- Dauerhafte Windows-Service-Bridge.
- Home Assistant REST/WebSocket Live-Integration.
- openHAB REST-Live-Integration.
- ioBroker/Node-RED Exporte.
- Signierte Firmware-/Profilpakete.
