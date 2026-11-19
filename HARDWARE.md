# Hardware-Empfehlung Füllstandsmessung

Beide Projekte sind für den **klassischen ESP32 (WROOM-32)** geschrieben. ESP32-C3/C6 haben GPIO 34 (Akku), GPIO 33 (Wakeup-Taster) und GPIO 25 (XSHUT) nicht in gleicher Form → bei einem Wechsel müssten Pins und Wakeup-Logik angepasst werden.

## Sender (ESP32_FillLevel_Sender, Akkubetrieb)

Entscheidend ist der **Ruhestrom des Boards im Deep Sleep**. Ein Standard-DevKit (CP2102/CH340, AMS1117, Power-LED) zieht auch im Deep Sleep ca. 5–15 mA → 18650-Akku nach 2–3 Wochen leer.

Empfohlene Boards:

| Board | Deep Sleep (Herstellerangabe) | Besonderheiten |
|---|---|---|
| **uPesy ESP32 Wroom Low Power DevKit** | ca. 10 µA | LiPo-Lader eingebaut |
| **DFRobot FireBeetle 2 ESP32-E** | ca. 10 µA | LiPo-Lader, JST-Akkuanschluss |

- Viele dieser Boards haben bereits einen Akku-Spannungsteiler an einem festen Pin → im Datenblatt prüfen und `VBAT_PIN` / `VBAT_DIVIDER` im Sketch anpassen (eigener Teiler entfällt).
- Sensor-Breakout: **VL53L1X mit sparsamem Regler und ohne Dauer-LED** (z. B. Pololu). GY-53-Module mit eigenem Mikrocontroller ungeeignet (Dauerstrom).

### Laufzeitabschätzung (Intervall 10 min)

| Verbraucher | pro Tag |
|---|---|
| 144 Wachphasen à ca. 1 s bei ~80 mA | ~3 mAh |
| Deep Sleep Board (~10 µA) | ~0,25 mAh |
| VL53L1X Standby (XSHUT) | ~0,1 mAh |

18650 (2500–3000 mAh): rechnerisch deutlich über ein Jahr, realistisch (Selbstentladung, Kälte) ca. 1 Jahr. Intervall 30 min ≈ dreifache Laufzeit.

## Empfänger (ESP32_FillLevel_Receiver, Netzteil)

Stromverbrauch egal → **beliebiges ESP32-DevKit** (z. B. ESP32-WROOM-32 DevKitC).

- **4 MB Flash** (Partitionstabelle: 2 OTA-Slots + LittleFS)
- **GPIO 33/32** (SDA/SCL, gleiche Leiste wie 3V3/5V) für das OLED (SH1106, I²C 0x3C)
- **WROOM-32U** mit externer Antenne, wenn der Empfänger weit vom Tank entfernt steht (Pinbelegung identisch)
