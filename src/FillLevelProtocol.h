// SPDX-License-Identifier: MIT
// FillLevelProtocol.h – gemeinsames ESP-NOW-Datenpaket für Sender und Empfänger.
// WICHTIG: Diese Datei muss in ESP32_FillLevel_Sender und ESP32_FillLevel_Receiver identisch sein.
#pragma once
#include <stdint.h>

#define FILL_PROTO_MAGIC    0x4C46   // "FL"
#define FILL_PROTO_VERSION  1

enum FillStatus : uint8_t {
    FILL_OK           = 0,   // Messung gültig
    FILL_SENSOR_ERROR = 1,   // Sensor nicht gefunden / Init fehlgeschlagen
    FILL_NO_VALID     = 2    // Sensor ok, aber keine gültige Messung (Signal zu schwach, außer Reichweite)
};

// Feld status: untere 4 Bit = FillStatus, obere Bits = Zustands-Flags des Senders
#define FILL_STATUS_MASK    0x0F
#define FILL_FLAG_TESTMODE  0x80     // Sender ist im Testmodus (kein Deep Sleep, Messung alle paar Sekunden)
#define FILL_FLAG_OTAMODE   0x40     // Sender ist im OTA-Modus (kein Deep Sleep, im Heim-WLAN, Update per /update)

typedef struct __attribute__((packed)) {
    uint16_t magic;          // FILL_PROTO_MAGIC
    uint8_t  version;        // FILL_PROTO_VERSION
    uint8_t  status;         // FillStatus
    uint32_t seq;            // laufende Nummer (verlorene Pakete erkennen)
    uint16_t distance_mm;    // Abstand Sensor -> Oberfläche (Median)
    uint16_t spread_mm;      // Streuung der Einzelmessungen (max - min), Qualitätsindikator
    uint16_t vbat_mV;        // Akkuspannung
    uint16_t interval_s;     // Messintervall des Senders (Empfänger erkennt Ausfall)
} FillPacket;                // 16 Byte

static_assert(sizeof(FillPacket) == 16, "FillPacket muss 16 Byte groß sein");

// ----- Kopplung (Pairing) -----
// Der Sender schickt als Broadcast eine Anfrage (FILL_PAIR_REQUEST) und probiert dabei alle Funkkanäle durch.
// Der Empfänger (im Kopplungsmodus oder bei bekanntem Sender) antwortet als Broadcast mit seiner MAC und seinem Kanal.
#define FILL_PAIR_MAGIC     0x5046   // "PF"
#define FILL_PAIR_REQUEST   1
#define FILL_PAIR_RESPONSE  2

typedef struct __attribute__((packed)) {
    uint16_t magic;          // FILL_PAIR_MAGIC
    uint8_t  version;        // FILL_PROTO_VERSION
    uint8_t  type;           // FILL_PAIR_REQUEST / FILL_PAIR_RESPONSE
    uint8_t  channel;        // Antwort: WLAN-Kanal des Empfängers
    uint8_t  rxMac[6];       // Antwort: STA-MAC des Empfängers
    uint8_t  txMac[6];       // Anfrage: STA-MAC des Senders; Antwort: Kopie (adressiert den Sender)
    uint8_t  reserved;
} FillPairPacket;            // 18 Byte, bewusst != sizeof(FillPacket)

static_assert(sizeof(FillPairPacket) != sizeof(FillPacket), "Pairing-Paket muss sich in der Länge vom Datenpaket unterscheiden");

// ----- Befehle Empfänger -> Sender -----
// Der Sender lauscht nach jedem bestätigten Datenpaket kurz (FILL_CMD_WAIT_MS). Liegt für ihn ein Befehl vor,
// antwortet der Empfänger sofort als Broadcast (txMac adressiert den Sender). Der Sender übernimmt den Befehl
// und schickt zur Bestätigung gleich ein weiteres Datenpaket mit dem neuen Zustand (FILL_FLAG_TESTMODE,
// FILL_FLAG_OTAMODE bzw. interval_s). Danach lauscht er erneut kurz, so dass mehrere ausstehende Befehle nacheinander ankommen.
// Geht ein Befehl verloren, wiederholt der Empfänger ihn beim nächsten Datenpaket.
// Ältere Sender ignorieren unbekannte Befehle (ohne Bestätigung) – der Empfänger wiederholt sie dann weiter.
#define FILL_CMD_MAGIC      0x4346   // "FC"
#define FILL_CMD_TESTMODE   1        // value: 0 = Testmodus aus, 1 = ein
#define FILL_CMD_INTERVAL   2        // value | value_hi << 8: neues Messintervall in Sekunden
#define FILL_CMD_OTAMODE    3        // value: 0 = OTA-Modus aus, 1 = ein
#define FILL_CMD_WAIT_MS    60
#define FILL_INTERVAL_MIN_S 30       // erlaubter Bereich des Messintervalls (Sender begrenzt ebenso)
#define FILL_INTERVAL_MAX_S 65000

typedef struct __attribute__((packed)) {
    uint16_t magic;          // FILL_CMD_MAGIC
    uint8_t  version;        // FILL_PROTO_VERSION
    uint8_t  cmd;            // FILL_CMD_...
    uint8_t  value;          // Befehlswert (untere 8 Bit)
    uint8_t  txMac[6];       // STA-MAC des Senders, für den der Befehl bestimmt ist
    uint8_t  value_hi;       // obere 8 Bit des Befehlswerts (bisher "reserved", bei FILL_CMD_TESTMODE 0)
} FillCmdPacket;             // 12 Byte

static_assert(sizeof(FillCmdPacket) != sizeof(FillPacket) && sizeof(FillCmdPacket) != sizeof(FillPairPacket),
              "Befehlspaket muss sich in der Länge von Daten- und Pairing-Paket unterscheiden");
