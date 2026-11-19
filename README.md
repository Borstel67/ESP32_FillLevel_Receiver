# ESP32_FillLevel_Receiver

Füllstandsanzeige: empfängt die Rohdaten von bis zu **4 Sendern** `ESP32_FillLevel_Sender` (je ein Behälter) per **ESP-NOW**, berechnet Füllstand und Inhalt, zeigt alles auf einem **SH1106 1,3" OLED** an und meldet die Werte per **MQTT** an Home Assistant (Auto-Discovery). Basis ist `ESP32_WiFiManager` (Captive-Portal, STA-UI, ElegantOTA).

## Bedienung
- Heim-WLAN: `http://fuellstand-anzeige.local/fill`
- Portal (keine WLAN-Daten / Verbindung verloren): WLAN **Fuellstand-Anzeige** (offen, ohne Passwort) → `http://192.168.4.1` – **nur WLAN-Konfiguration**; `/fill` und OTA `/update` nur im Heim-WLAN.
- Startseiten: im Portal `indexAP.html` (nur WLAN wählen, Passwort, optional IP/Hostname/NTP), im Heim-WLAN `indexSTA.html` (Füllstand, Netzwerk-Einstellungen, Firmware-Update). Nach Änderungen in `data/` LittleFS neu hochladen.
- BOOT-Taster: beim Start 2–5 s = Portal, ≥ 5 s = WLAN-Daten löschen; im Betrieb kurz drücken = Portal.

Seite **/fill**: Auswahl des Behälters, Live-Werte, Kalibrierung und Geometrie je Behälter, Sender verwalten (koppeln, unbekannten Sender übernehmen, MAC von Hand, entfernen), Display, MQTT.

## Mehrere Sender
- Bis zu `MAX_SENDERS` = 4 Sender, jeder Sender ist ein eigener Behälter mit Name, Form, Abständen VOLL/LEER und Inhalt.
- Neuer Sender: „Kopplung starten“ (2 min) → am Sender „Empfänger suchen“. Er belegt den nächsten freien Platz.
- Nur eingetragene Sender werden ausgewertet. Andere erscheinen auf `/fill` als „unbekannter Sender“ und lassen sich mit einem Klick übernehmen. Solange noch gar kein Sender eingetragen ist, wird der erste automatisch übernommen.
- OLED: blättert alle 5 s zum nächsten **aktiven** Behälter (Name + `1/3` oben). Jeder Behälter lässt sich auf `/fill` ein-/ausschalten (Häkchen „Aktiv“). Aus = wie nicht vorhanden: kein Display, Messwerte verworfen, in Home Assistant „nicht verfügbar“; die Einstellungen bleiben gespeichert.
- Testmodus eines Senders (kein Deep Sleep, Messung alle 5 s) auf `/fill` bzw. in der App ein-/ausschalten: Der Befehl geht mit dem nächsten Datenpaket per ESP-NOW an den Sender, auch wenn dieser gerade schläft (`POST /api/fill/testmode`).
- OTA-Modus eines Senders (kein Deep Sleep, Sender im Heim-WLAN, Update über `/update`) getrennt vom Testmodus schalten – gleicher Ablauf (`POST /api/fill/otamode`, Zustand `ota_mode`/`ota_req` in `/api/fill/state`).
- Update von 1.0: der bisherige Behälter (Geometrie, MQTT-ID) wird automatisch Platz 1 „Füllstand“.

## Berechnung
- *Abstand bei VOLL* / *LEER*: Sensor → Wasseroberfläche (mm). Mit „↓“ wird der aktuelle Messwert übernommen.
- **stehend** (Tonne, IBC, Quader): Inhalt linear zur Höhe.
- **liegender Zylinder**: Kreissegment-Formel, Durchmesser = Abstand leer − Abstand voll.
- Sender gilt als offline nach 3 verpassten Intervallen.

## Hardware
Board-Empfehlung und Laufzeitabschätzung: siehe [HARDWARE.md](HARDWARE.md).

## Verdrahtung
| ESP32 | Anschluss |
|---|---|
| GPIO 33 / 32 | OLED SDA / SCL (I²C-Adresse 0x3C) |
| 3V3 / GND | OLED VCC / GND |
| GPIO 0 | BOOT-Taster (auf dem Board) |
| GPIO 2 | Status-LED |

## MQTT
| Topic | Inhalt |
|---|---|
| `<basis>/<id>/state` | JSON je Behälter: level_pct, volume_l, level_cm, distance_mm, spread_mm, battery_v, battery_pct, rssi, seq, lost, status |
| `<basis>/<id>/sender` | `online` / `offline` (Sender liefert Daten) |
| `<basis>/<id>/avail` | `online` / `offline` (Behälter ein-/ausgeschaltet) |
| `<basis>/status` | `online` / `offline` (LWT, Verfügbarkeit der Anzeige) |
| `homeassistant/.../<id>/.../config` | Discovery, **ein HA-Gerät je Behälter** (Gerätename = Behältername): Füllstand, Inhalt, Pegel, Akku, Akkuspannung, Sensorabstand, Funksignal, Verlorene Pakete, Sender |

`<id>` = `fuellstand_` + letzte 3 Byte der Sender-MAC (beim aus Version 1.0 übernommenen Behälter die bisherige ID, damit HA-Entitäten und Verlauf erhalten bleiben). Wird ein Sender entfernt, löscht der Empfänger dessen Discovery-Einträge.

## Bibliotheken
Wie Basisprojekt (ElegantOTA, ESPAsyncWebServer, AsyncTCP, ArduinoJson) **plus**:
- **Adafruit SH110X** + **Adafruit GFX** + Adafruit BusIO
- **PubSubClient**

## Einrichtung
1. Partitionen wie Basisprojekt, LittleFS hochladen: `.\LittleFS.ps1 -Port COM4`
2. Sketch flashen, über das Portal ins Heim-WLAN bringen.
3. Kopplung je Sender: am Empfänger unter `/fill` „Kopplung starten“ (2 min), dann am Sender unter `/fill` „Empfänger suchen“.
   MAC und Kanal werden beidseitig automatisch gespeichert. Für weitere Sender wiederholen.
   Von Hand geht es weiterhin: Empfänger-MAC beim Sender eintragen, dann am Empfänger den „unbekannten Sender“ übernehmen.
4. Je Behälter: Namen vergeben, vermessen bzw. „↓“-Übernahme bei vollem/leerem Behälter, Inhalt eintragen. MQTT einrichten.

## Dateien
- `ESP32_FillLevel_Receiver.ino` – ESP-NOW-Empfang, Berechnung, OLED, MQTT, WiFiManager
- `src/FillLevelProtocol.h` – Datenpaket (**identisch im Sender halten!**)
- `src/ReceiverPage.h` – Seite `/fill`
- `src/ESPWiFiManager*` – aus ESP32_WiFiManager (ergänzt um `server()`-Zugriff für eigene Routen)

## Lizenz
MIT – siehe [LICENSE](LICENSE).
