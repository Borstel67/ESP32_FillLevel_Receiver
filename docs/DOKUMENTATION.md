# Dokumentation ESP32_FillLevel_Receiver

Füllstandsanzeige für Tanks und Behälter. Der Empfänger nimmt die Rohdaten (Abstand, Akkuspannung) von bis zu **4 Sendern** `ESP32_FillLevel_Sender` per **ESP-NOW** entgegen – jeder Sender ist ein eigener Behälter –, berechnet Füllstand und Inhalt, zeigt sie auf einem **SH1106-OLED (1,3")** an und meldet sie per **MQTT** an Home Assistant (Auto-Discovery).

Firmware-Version: 1.1.0 · Basis: `ESP32_WiFiManager` (Captive-Portal, STA-Oberfläche, ElegantOTA, ArduinoOTA)

## Inhalt

1. [Systemübersicht](#1-systemübersicht)
2. [Hardware und Verdrahtung](#2-hardware-und-verdrahtung)
3. [Entwicklungsumgebung und Bibliotheken](#3-entwicklungsumgebung-und-bibliotheken)
4. [Inbetriebnahme](#4-inbetriebnahme)
5. [Bedienung](#5-bedienung)
6. [Berechnung von Füllstand und Inhalt](#6-berechnung-von-füllstand-und-inhalt)
7. [Konfiguration](#7-konfiguration)
8. [Funkprotokoll (ESP-NOW)](#8-funkprotokoll-esp-now)
9. [MQTT und Home Assistant](#9-mqtt-und-home-assistant)
10. [Projektstruktur](#10-projektstruktur)
11. [Fehlersuche](#11-fehlersuche)

---

## 1. Systemübersicht

```
 ┌──────────────────┐   ESP-NOW (16-Byte-Paket)   ┌────────────────────┐   WLAN / MQTT   ┌──────────────────┐
 │ Sender (Akku)    │ ──────────────────────────▶ │ Empfänger (Netz)   │ ──────────────▶ │ Home Assistant   │
 │ VL53L1X, Deep    │   Abstand, Akku, Status     │ Berechnung, OLED,  │                 │ (Auto-Discovery) │
 │ Sleep            │                             │ Web-UI /fill       │                 └──────────────────┘
 └──────────────────┘                             └────────────────────┘
```

- Der Sender misst den Abstand zur Flüssigkeitsoberfläche und schläft danach bis zum nächsten Intervall.
- Der Empfänger rechnet den Abstand mit der gespeicherten Tankgeometrie in Prozent, Liter und Zentimeter um.
- Der Funkkanal ist der Kanal des Heim-WLANs. Der Sender findet ihn selbst.

## 2. Hardware und Verdrahtung

Beliebiges ESP32-DevKit mit 4 MB Flash (z. B. ESP32-WROOM-32 DevKitC). Bei großem Abstand zum Tank hilft ein WROOM-32U mit externer Antenne. Details zu Sender-Boards und Laufzeit: [HARDWARE.md](../HARDWARE.md).

| ESP32 | Anschluss |
|---|---|
| GPIO 33 / 32 | OLED SDA / SCL (I²C-Adresse 0x3C) |
| 3V3 / GND | OLED VCC / GND |
| GPIO 0 | BOOT-Taster (auf dem Board) |
| GPIO 2 | Status-LED |

### Anschlussplan

```
        ESP32-DevKit (WROOM-32)                    SH1106-OLED 1,3" (I²C)
   ┌─────────────────────────────┐               ┌──────────────────────┐
   │                        3V3  ├───────────────┤ VCC                  │
   │                        GND  ├───────────────┤ GND                  │
   │                     GPIO 32 ├───────────────┤ SCL                  │
   │                     GPIO 33 ├───────────────┤ SDA                  │
   │                             │               │ Adresse 0x3C         │
   │                             │               └──────────────────────┘
   │                     GPIO 0  ├─ BOOT-Taster ── GND  (auf dem Board, LOW-aktiv)
   │                     GPIO 2  ├─ Status-LED          (auf dem Board)
   │                             │
   │                        USB  ├─ 5-V-Netzteil (Versorgung und Flashen)
   │                             │
   │                      5V/VIN ├─ + 5-V-Netzteil  (alternativ zu USB)
   │                        GND  ├─ − 5-V-Netzteil
   └─────────────────────────────┘
```

- Versorgung entweder über **USB** oder über den Pin **5V** (je nach Board als `5V` oder `VIN` beschriftet) mit einem stabilisierten 5-V-Netzteil (≥ 500 mA). Den Minuspol an GND legen. Am 5-V-Pin nur 5 V anschließen, niemals mehr. Den 3V3-Pin nicht von außen speisen.
- USB und 5-V-Pin nicht gleichzeitig an zwei Netzteile hängen. Viele DevKits haben keine Schutzdiode zwischen USB und 5V. Zum Flashen über USB das externe Netzteil abziehen.

- Das OLED unbedingt an **3V3** anschließen, nicht an 5 V. Die Pin-Reihenfolge am Modul (VCC/GND oder GND/VCC zuerst) unterscheidet sich je nach Hersteller, deshalb die Beschriftung auf der Platine beachten.
- Alle vier OLED-Leitungen liegen auf derselben Stiftleiste wie 3V3 und 5V (linke Leiste beim ESP32-DevKitC: 3V3 … GPIO 32, GPIO 33 … GND … 5V). Das OLED lässt sich so mit einem kurzen Kabelstrang einseitig anschließen.
- Die Pull-up-Widerstände für SDA/SCL sitzen auf üblichen OLED-Modulen bereits drauf.
- BOOT-Taster und Status-LED sind auf dem DevKit vorhanden; es ist keine zusätzliche Verdrahtung nötig. Soll eine externe LED genutzt werden: GPIO 2 → 330-Ω-Widerstand → LED-Anode, Kathode an GND.
- Die Pins sind in `ESP32_FillLevel_Receiver.ino` als `LED_PIN`, `BTN_PIN`, `OLED_SDA`, `OLED_SCL` und `OLED_ADDR` festgelegt.

Partitionstabelle (`partitions.csv`, 4 MB):

| Name | Typ | Offset | Größe |
|---|---|---|---|
| nvs | data | 0x9000 | 0x5000 |
| otadata | data | 0xE000 | 0x2000 |
| app0 | ota_0 | 0x10000 | 0x1C0000 |
| app1 | ota_1 | 0x1D0000 | 0x1C0000 |
| coredump | data | 0x390000 | 0x10000 |
| littlefs | data | 0x3A0000 | 0x60000 |

Die Firmware belegt rund 1,2 MB (92 % der 1,25-MB-Standardpartition). Mit der eigenen Tabelle (1,75 MB pro App-Slot) bleibt genug Reserve. Die eigene `partitions.csv` muss deshalb unbedingt verwendet werden.

## 3. Entwicklungsumgebung und Bibliotheken

Entwickelt mit Visual Studio + Visual Micro, Board **ESP32-WROOM-DA Module** (`esp32:esp32:esp32da`), ESP32-Core 3.3.12.

Benötigte Bibliotheken:

| Bibliothek | Getestete Version |
|---|---|
| ElegantOTA | 4.0.0 |
| ESP Async WebServer | 3.12.1 |
| Async TCP | 3.5.0 |
| ArduinoJson | 7.4.3 |
| PubSubClient | 2.8 |
| Adafruit GFX Library | 1.12.6 |
| Adafruit SH110X | 2.1.15 |
| Adafruit BusIO | 1.17.4 |

Installation per Kommandozeile:

```powershell
arduino-cli lib install "Adafruit GFX Library" "Adafruit SH110X" "Adafruit BusIO"
```

### Pflicht-Anpassung an ElegantOTA

Das `#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1` in der `.ino` wirkt nur auf die Sketch-Datei. ElegantOTA selbst wird als eigene Bibliothek mit dem Standardwert 0 kompiliert. Ohne Anpassung scheitert das Linken mit:

```
undefined reference to `ElegantOTAClass::begin(AsyncWebServer*, char const*, char const*)'
```

Abhilfe: In `Documents\Arduino\libraries\ElegantOTA\src\ElegantOTA.h` den Standardwert ändern:

```cpp
#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER
  #define ELEGANTOTA_USE_ASYNC_WEBSERVER 1   // war 0
#endif
```

Die Änderung liegt außerhalb des Projekts und geht bei einem Bibliotheks-Update oder auf einem neuen Rechner verloren.

## 4. Inbetriebnahme

1. Bibliotheken installieren und ElegantOTA anpassen (Kapitel 3).
2. Dateisystem (LittleFS, Ordner `data\`) hochladen: `.\LittleFS.ps1 -Port COM4` (Port anpassen).
3. Sketch flashen.
4. Über das Portal ins Heim-WLAN bringen (Kapitel 5).
5. Sender koppeln: `http://fuellstand-anzeige.local/fill` öffnen, unter „Sender verwalten“ **Kopplung starten**, dann am Sender „Empfänger suchen“. Für jeden weiteren Sender wiederholen (max. 4).
6. Je Behälter (oben auswählen): Namen vergeben, bei vollem und bei leerem Behälter jeweils „↓“ drücken, um den aktuellen Messwert zu übernehmen. Danach den Inhalt in Litern eintragen.
7. Optional MQTT einrichten.

Updates im Betrieb: über ElegantOTA im Browser (nur im Heim-WLAN) oder per ArduinoOTA (Port 3232). Die Skripte `Firmware-OTA.ps1` und `LittleFS-OTA.ps1` im Projektordner sind dafür gedacht.

**Rollback bei OTA-Fehlern** (ab 1.4.1, `src/OtaRollback.h`, header-only):
- Bricht der Upload ab oder ist er fehlerhaft, bleibt die Boot-Partition unverändert. Die bisherige Firmware läuft weiter, der Fehler steht im seriellen Log.
- Eine neu geladene Firmware gilt zunächst nur als vorläufig. Bestätigt wird sie erst, wenn sie 30 s ununterbrochen im Heim-WLAN läuft und der Webserver (mit `/update`) aktiv ist.
- Stürzt sie vorher ab oder wird sie innerhalb von 5 min nicht bestätigt, startet der ESP32 wieder mit der vorherigen Firmware. Das gilt z. B. auch, wenn sie das WLAN nicht erreicht.
- Auf der Seite `/fill` zeigt die Zeile „Firmware“ den Zustand („wird nach Selbsttest bestätigt“ bzw. „Rollback: … wurde verworfen“). Die Web-API liefert ihn in `/api/fill/state` als `fw`, `ota_pending` und `ota_info`.
- Achtung: Wenn das WLAN in den ersten 5 min nach einem Update ausfällt, wird ebenfalls zurückgesprungen. Das Update muss dann wiederholt werden.

## 5. Bedienung

| Zugang | Adresse / Name |
|---|---|
| Heim-WLAN | `http://fuellstand-anzeige.local/fill` |
| Portal (keine WLAN-Daten oder Verbindung verloren) | WLAN **Fuellstand-Anzeige** (offen, ohne Passwort), dann `http://192.168.4.1` – nur WLAN-Konfiguration |

Das Portal dient ausschließlich der WLAN-Konfiguration; die Seite **/fill** und ElegantOTA (`/update`) sind nur im Heim-WLAN erreichbar. Das Portal öffnet sich automatisch, wenn keine WLAN-Daten gespeichert sind oder die Verbindung länger als 60 s fehlt. Es schließt nach 300 s ohne Eingabe.

**BOOT-Taster (GPIO 0):**

| Aktion | Wirkung |
|---|---|
| Beim Start 2–5 s halten | Portal starten |
| Beim Start ≥ 5 s halten | gespeicherte WLAN-Daten löschen |
| Im Betrieb kurz drücken (0,7–3 s) | Portal starten |
| Im Betrieb ≥ 10 s halten | **Werkseinstellung** inkl. WLAN (ab 3 s Countdown im Display; vorher loslassen = Abbruch) |

**Werkseinstellung** (ab 1.4.1):
- Auf der Seite `/fill` unten gibt es die Abschnitte „Konfiguration löschen (WLAN behalten)“ und „Alles löschen inkl. WLAN“. Am Gerät geht es mit dem BOOT-Taster (≥ 10 s). Die Web-API dafür ist `POST /api/fill/factory` mit `confirm=1` und `wifi=0|1`.
- Gelöscht werden der NVS-Namespace `fill` (alle Sender und Behälter samt Kalibrierung und Akku-Grenzen, MQTT, Display) und auf Wunsch die WLAN-Daten (`/config.json`).
- Ist MQTT verbunden, werden die Behälter vorher auch aus Home Assistant entfernt. Danach startet der Empfänger neu.
- Die Sender behalten ihre Einstellungen und müssen neu gekoppelt werden.

Die Seite **/fill** zeigt oben die eingetragenen Behälter als Auswahl (Name und Füllstand). Für den ausgewählten Behälter folgen Live-Werte, Kalibrierung und Geometrie. Darunter: Sender verwalten, Display und MQTT.

### Mehrere Sender

| Funktion | Ablauf |
|---|---|
| Sender koppeln | „Kopplung starten“ (120 s), dann am Sender „Empfänger suchen“. Der Sender belegt den nächsten freien Platz und ist danach ausgewählt. Sind alle 4 Plätze belegt, wird die Kopplung abgelehnt. |
| Unbekannter Sender | Pakete von nicht eingetragenen Sendern werden verworfen. Der zuletzt gehörte erscheint als „unbekannter Sender“ und lässt sich mit einem Klick übernehmen (z. B. wenn die Empfänger-MAC am Sender von Hand eingetragen wurde). |
| Erster Sender | Solange gar kein Sender eingetragen ist, wird der erste, der Daten sendet, automatisch übernommen. |
| MAC von Hand | Sender-MAC eintragen → „Hinzufügen“. |
| Sender entfernen | Im ausgewählten Behälter „Sender entfernen“. Geometrie und das Home-Assistant-Gerät werden gelöscht. |
| Behälter ein/aus | Häkchen „Aktiv“ in der Senderliste oder in den Einstellungen des Behälters. Ein ausgeschalteter Behälter verhält sich wie nicht vorhanden: nicht auf dem OLED, Messwerte werden verworfen, keine MQTT-Werte, in Home Assistant „nicht verfügbar“ (Entitäten und Verlauf bleiben). Name, Geometrie und MAC bleiben gespeichert; auf `/fill` erscheint er ausgegraut mit Einschalter. |
| Testmodus des Senders | Häkchen „Testmodus“ beim ausgewählten Behälter (oder App, Tab „Behälter“; API `POST /api/fill/testmode idx=<n> enabled=0\|1`). Der Empfänger schickt den Befehl (`FillCmdPacket`) direkt nach dem nächsten Datenpaket dieses Senders. Der Sender lauscht nach jedem bestätigten Paket `FILL_CMD_WAIT_MS` (60 ms), übernimmt den Befehl, speichert ihn und bestätigt sofort mit einem weiteren Paket. Aus dem Deep Sleep dauert das also bis zu einem Messintervall; bis dahin steht „angefordert“. Den aktuellen Zustand meldet der Sender in jedem Paket (Bit `FILL_FLAG_TESTMODE` im Feld `status`); er steht auch im MQTT-State als `test_mode`. Geht der Befehl verloren, wird er beim nächsten Paket wiederholt. Ab Empfänger 1.3.0 – der Sender braucht die Firmware mit derselben `FillLevelProtocol.h`. |
| Akkuspannung 0 % / 100 % | Je Behälter (Standard 3,30 V / 4,20 V, Li-Ion 1S), in der Android-App Tab „Behälter“ → „Akku des Senders“; API `POST /api/fill/tank … bat_empty_mv=<mV> bat_full_mv=<mV>` (optional, fehlt das Feld, bleibt der Wert; Bereich 2000 … 6000 mV, 100 % mindestens 100 mV über 0 %). NVS-Keys `t<n>bemp` / `t<n>bful`. `batteryPercent()` bildet die gemessene Spannung linear von [0 %, 100 %] auf die Li-Ion-Stützpunkte 3,30 … 4,20 V ab und interpoliert dann entlang der Kurve – die Kurvenform bleibt, nur die Grenzen verschieben sich. Ab Empfänger 1.4.0. |
| Messintervall des Senders | Android-App, Tab „Sender Empfänger“ → „Messintervall“ (API `POST /api/fill/interval idx=<n> interval_s=30…65000`). Läuft wie der Testmodus über `FillCmdPacket` (Befehl `FILL_CMD_INTERVAL`, Wert in Sekunden als `value` + `value_hi`, früher `reserved`). Der Sender speichert das Intervall im NVS und bestätigt mit einem Paket, dessen `interval_s` den neuen Wert trägt; bis dahin meldet `/api/fill/state` `interval_req`. Stehen Testmodus und Intervall gleichzeitig aus, geht je Paket ein Befehl raus (Testmodus zuerst); der Sender lauscht nach jeder Bestätigung erneut und bekommt beide im selben Wachzyklus. Ab Empfänger 1.4.0; ältere Sender ignorieren den Befehl (bleibt „angefordert“). |

Das OLED blättert alle 5 s (`OLED_PAGE_MS`) zum nächsten aktiven Behälter. In der Kopfzeile stehen Name und Seitenzähler der aktiven Behälter (z. B. `2/3`). Ohne eingetragenen Sender zeigt es „Kein Sender eingetragen“, sind alle ausgeschaltet „Alle Behälter ausgeblendet“.

**Update von Version 1.0:** Beim ersten Start wird die bisherige Konfiguration übernommen. Der Behälter wird Platz 1 mit dem Namen „Füllstand“ und behält seine MQTT-ID; Home Assistant führt die Entitäten samt Verlauf weiter. War vorher kein Sender-Filter gesetzt, wird Platz 1 mit dem ersten Sender belegt, der sich meldet.

Das Portal-WLAN ist offen (`AP_PASS` ist im Quelltext leer). Jeder in Funkreichweite kann sich verbinden, solange das Portal aktiv ist, und dort WLAN-Daten eintragen. Ein Passwort lässt sich über `AP_PASS` (mindestens 8 Zeichen) wieder setzen.

## 6. Berechnung von Füllstand und Inhalt

Die Berechnung steht in `computeLevel()`. Eingaben sind der gemessene Abstand `distance_mm` (Sensor → Oberfläche) und die Konfiguration `distEmpty`, `distFull` und `capacityL`.

1. Nutzbare Höhe: `H = distEmpty − distFull`
2. Pegel über „leer“: `lvl = distEmpty − distance_mm`, begrenzt auf 0 bis H
3. Relative Füllhöhe: `h = lvl / H` (0 bis 1)
4. Füllanteil `frac` je nach Tankform
5. Ergebnis:
   - `volumeL = frac × capacityL`
   - `levelPct = frac × 100`
   - `levelCm = lvl / 10`

### Stehender Tank (Zylinder oder Quader, `shape = 0`)

Der Querschnitt ist über die Höhe konstant, das Volumen wächst linear:

```
frac = h
```

`capacityL` ist der Inhalt zwischen den Marken „leer“ und „voll“. Das ist nicht zwingend der Nenninhalt des Tanks. Der Durchmesser oder die Kantenlängen werden nicht benötigt.

Alternativ rechnet die Web-Oberfläche den Inhalt aus Maßen in cm (wird nur ins Feld übernommen, nicht gespeichert): stehend `Breite × Tiefe × Höhe` oder `π·(Ø/2)² × Höhe`, liegend `π·(Ø/2)² × Länge`. Leere Höhe bzw. leerer Ø (liegend) = `distEmpty − distFull`.

Beispiel: `distEmpty` = 1500 mm, `distFull` = 200 mm, `capacityL` = 1000 L, Messung 850 mm. Pegel = 650 mm, H = 1300 mm, h = 0,5, also 500 L und 50 %.

Nicht abgebildet werden Tanks mit konischem oder gewölbtem Boden und Tanks mit Einbauten.

### Liegender Zylinder (`shape = 1`)

Das Volumen ist nicht proportional zur Höhe. Es gilt die Kreissegment-Formel:

```
θ    = 2 · acos(1 − 2h)
frac = (θ − sin θ) / (2π)
```

Der Durchmesser des Zylinders entspricht `distEmpty − distFull`. Die beiden Marken müssen daher genau Boden und Decke des Zylinders treffen.

### Gültigkeit

Es wird nur gerechnet, wenn der Sender `FILL_OK` meldet und `distEmpty > distFull` gilt. Andernfalls ist `levelOk` false und die Werte werden nicht aktualisiert.

### Sender-Ausfall

Der Sender gilt als offline, wenn länger als `3 × Intervall + 60 s` kein Paket eingeht (ohne Angabe im Paket: Intervall 600 s).

### Akkuanzeige

Der Sender läuft mit einem Li-Ion-Akku 3,7 V (1S, Ladeschluss 4,2 V). Die gemessene Spannung wird über die Entladekurve in Prozent umgerechnet (`BAT_CURVE` im Sketch, zwischen den Stützpunkten linear):

| mV | ≤ 3300 | 3500 | 3600 | 3700 | 3750 | 3800 | 3900 | 4000 | 4100 | ≥ 4200 |
|---|---|---|---|---|---|---|---|---|---|---|
| % | 0 | 5 | 10 | 25 | 40 | 50 | 65 | 78 | 90 | 100 |

Gemessen wird kurz nach dem Aufwachen unter Last; die Anzeige ist ein Richtwert. Bei angeschlossenem USB-Ladekabel zeigt der Sender die Ladespannung (≈ 100 %).

## 7. Konfiguration

Alle Einstellungen liegen im NVS (Namespace `fill`) und werden über die Seite `/fill` geändert. `ver` = 2 kennzeichnet das Layout für mehrere Sender.

Je Behälter (Platz `n` = 0…3, Key-Präfix `t<n>`, z. B. `t0dfull`):

| Einstellung | NVS-Key | Standard | Bedeutung |
|---|---|---|---|
| Sender-MAC | `t<n>mac` | leer | STA-MAC des Senders, leer = Platz frei |
| Name | `t<n>name` | `Behälter <n+1>` | Anzeige auf OLED, Web und als HA-Gerätename |
| Aktiv | `t<n>act` | an | aus = wie nicht vorhanden (kein Display, kein MQTT, Pakete verworfen) |
| MQTT-ID | `t<n>uid` | `fuellstand_<MAC>` | eindeutige ID für Topics und HA, ändert sich beim Umbenennen nicht |
| Form | `t<n>shape` | 0 | 0 = stehend (linear), 1 = liegender Zylinder |
| Abstand bei VOLL | `t<n>dfull` | 100 mm | Sensor → Oberfläche bei vollem Tank |
| Abstand bei LEER | `t<n>dempty` | 1500 mm | Sensor → Oberfläche bei leerem Tank |
| Inhalt bei voll | `t<n>cap` | 1000 L | Volumen zwischen den Marken |

Global:

| Einstellung | NVS-Key | Standard | Bedeutung |
|---|---|---|---|
| Display drehen | `flip` | aus | OLED um 180° drehen |
| MQTT aktiv | `mon` | aus | |
| MQTT-Host / Port | `mhost` / `mport` | – / 1883 | |
| MQTT-Benutzer / Passwort | `muser` / `mpass` | leer | Passwort wird nie an die Weboberfläche ausgeliefert, ein leeres Feld behält das gespeicherte |
| MQTT-Basistopic | `mbase` | `fuellstand` | |

## 8. Funkprotokoll (ESP-NOW)

Definiert in `src/FillLevelProtocol.h`. **Die Datei muss im Sender- und im Empfängerprojekt identisch sein.** Pakete mit falscher Länge werden verworfen.

| Feld | Typ | Bedeutung |
|---|---|---|
| `magic` | uint16 | `0x4C46` („FL“) |
| `version` | uint8 | Protokollversion, aktuell 1 |
| `status` | uint8 | siehe unten |
| `seq` | uint32 | laufende Nummer, daraus ergeben sich verlorene Pakete |
| `distance_mm` | uint16 | Abstand Sensor → Oberfläche (Median) |
| `spread_mm` | uint16 | Streuung der Einzelmessungen (max − min), Qualitätsindikator |
| `vbat_mV` | uint16 | Akkuspannung |
| `interval_s` | uint16 | Messintervall des Senders, für die Ausfallerkennung |

Das Paket ist 16 Byte groß (per `static_assert` geprüft).

Statuswerte: `0` FILL_OK (Messung gültig), `1` FILL_SENSOR_ERROR (Sensor nicht gefunden), `2` FILL_NO_VALID (keine gültige Messung, z. B. Signal zu schwach).

## 9. MQTT und Home Assistant

| Topic | Inhalt |
|---|---|
| `<basis>/<id>/state` | JSON je Behälter mit `level_pct`, `volume_l`, `level_cm`, `distance_mm`, `spread_mm`, `battery_v`, `battery_pct`, `rssi`, `seq`, `lost`, `status` |
| `<basis>/<id>/sender` | `online` / `offline` (Sender liefert Daten) |
| `<basis>/<id>/avail` | `online` / `offline` (Behälter ein-/ausgeschaltet; zusammen mit `<basis>/status` die Verfügbarkeit in HA) |
| `<basis>/status` | `online` / `offline` (Last Will, Verfügbarkeit der Anzeige, gilt für alle Behälter) |
| `homeassistant/<komponente>/<id>/<key>/config` | Auto-Discovery |

`<id>` ist die MQTT-ID des Behälters (`t<n>uid`). Je Behälter entsteht ein eigenes Gerät in Home Assistant (Gerätename = Behältername) mit diesen Entitäten: Füllstand, Inhalt, Pegel, Akku, Akkuspannung, Sensorabstand, Funksignal, Verlorene Pakete und Sender. Beim Umbenennen wird die Discovery neu gesendet; beim Entfernen eines Senders werden seine Discovery-Einträge und Werte gelöscht (leere Retained-Nachrichten). Die Topics `<basis>/state` und `<basis>/sender` aus Version 1.0 werden beim Verbinden geleert.

Verbindungsversuche zum Broker wiederholen sich alle 15 s.

## 10. Projektstruktur

| Datei | Inhalt |
|---|---|
| `ESP32_FillLevel_Receiver.ino` | ESP-NOW-Empfang, Berechnung, OLED, MQTT, WiFiManager |
| `src/FillLevelProtocol.h` | Datenpaket (identisch zum Sender halten) |
| `src/ReceiverPage.h` | Seite `/fill` (Behälterauswahl, Sender verwalten) |
| `src/ESPWiFiManager*` | WLAN-Verwaltung aus `ESP32_WiFiManager` |
| `src/AWM_Logging.h` | Logging (Level 3 = INFO) |
| `data/` | Weboberfläche für LittleFS: `indexAP.html` (Startseite im Portal, nur WLAN einrichten), `indexSTA.html` (Startseite im Heim-WLAN mit Links zu `/fill`, Netzwerk-Einstellungen, Firmware-Update), `settings.html`, CSS |
| `partitions.csv` | Partitionstabelle |
| `board.txt` | Visual-Micro-Überschreibungen (Partitionen, LittleFS-Upload) |
| `LittleFS.ps1`, `LittleFS-OTA.ps1`, `Firmware-OTA.ps1` | Hilfsskripte für Upload und OTA |
| `HARDWARE.md` | Board-Empfehlung und Laufzeitabschätzung |

Der Pfad `runtime.tools.mklittlefs.path` in `board.txt` ist auf einen bestimmten Benutzer fest eingestellt und muss auf einem anderen Rechner angepasst werden.

## 11. Fehlersuche

| Symptom | Ursache und Abhilfe |
|---|---|
| `Adafruit_GFX.h: No such file or directory` | Adafruit-Bibliotheken fehlen, siehe Kapitel 3 |
| Linker: `undefined reference to ElegantOTAClass::begin(AsyncWebServer*, …)` | ElegantOTA nicht im Async-Modus, siehe Kapitel 3 |
| Upload: `PermissionError(13, 'Ein an das System angeschlossenes Gerät funktioniert nicht')` | COM-Port blockiert oder Verbindung verloren. Serial Monitor schließen, Board neu einstecken, anderes USB-Kabel (mit Datenleitung) probieren, ggf. BOOT gedrückt halten und EN antippen |
| Keine Werte auf dem Display / `/fill` | Sender eingetragen? (sonst erscheint er als „unbekannter Sender“ und muss übernommen werden), `FillLevelProtocol.h` auf beiden Seiten vergleichen, Abstand zwischen Sender und Empfänger verringern |
| Inhalt unplausibel | `distFull`, `distEmpty` und `capacityL` neu kalibrieren. Beim liegenden Zylinder muss der Durchmesser zu den Marken passen |
| Sender „offline“ | Seit über 3 Intervallen + 60 s kein Paket. Akku, Reichweite und Funkkanal des Senders prüfen |
