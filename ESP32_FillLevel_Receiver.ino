// SPDX-License-Identifier: MIT
// ESP32_FillLevel_Receiver – Füllstandsanzeige: ESP-NOW-Empfang, Berechnung, OLED (SH1106), MQTT/Home Assistant
// Basis: ESP32_WiFiManager (Captive-Portal, STA-UI, ElegantOTA, ArduinoOTA)
//
// Ablauf
//  - WiFiManager wie im Basisprojekt: Heim-WLAN, sonst Captive-Portal "Fuellstand-Anzeige" (http://192.168.4.1) nur zur WLAN-Konfiguration
//  - ESP-NOW empfängt die Rohdaten (Abstand, Akku) von bis zu MAX_SENDERS Sendern; der Funkkanal ist der Kanal
//    des Heim-WLANs. Die Sender finden diesen Kanal selbst.
//  - Jeder Sender = ein Behälter mit eigenem Namen, eigener Geometrie und Kalibrierung
//  - Berechnung Füllstand/Inhalt aus der Tankgeometrie (stehend = linear, liegender Zylinder)
//  - Behälter ein/aus: ausgeschaltete Behälter verhalten sich wie nicht vorhanden (kein Display, kein MQTT,
//    Pakete werden verworfen); Name, Geometrie und MAC bleiben gespeichert
//  - Anzeige auf SH1106 1,3" (I²C, blättert automatisch durch die aktiven Behälter), Weboberfläche /fill,
//    MQTT mit Home-Assistant-Discovery (ein HA-Gerät je Behälter)
//  - BOOT-Taster: wie Basisprojekt (beim Start 2–5 s = Portal, ≥ 5 s = WLAN-Daten löschen; im Betrieb kurz = Portal)
//
// Bibliotheken: ElegantOTA, ESPAsyncWebServer, AsyncTCP, ArduinoJson (wie Basisprojekt)
//               + Adafruit SH110X, Adafruit GFX, PubSubClient

#include <Arduino.h>
#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#endif
// Explizite Includes, damit Visual Micro die transitiven Abhängigkeiten
// (Include-Pfade) für ElegantOTA, WiFi und Adafruit_GFX auflöst.
#include <FS.h>
#include <Network.h>
#include <Update.h>
#include <LittleFS.h>
#include <AsyncUDP.h>
#include <NetworkClientSecure.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <SHA1Builder.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <Adafruit_BusIO_Register.h>
#include <Adafruit_I2CDevice.h>
#include <ElegantOTA.h>
#include <WiFi.h>
#include <Wire.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Must precede all functions: Arduino's auto-generated prototypes are inserted before the first function.
struct HaEntity {
	const char* component; const char* key; const char* name; const char* unit;
	const char* devClass;  const char* valueTpl; const char* icon; bool diagnostic;
};
#include <src/ESPWiFiManagerAP.h>
#include <src/ESPWiFiManagerSTA.h>
#include <src/ESPWiFiManagerCommon.h>
#include <src/FillLevelProtocol.h>
#include <src/ReceiverPage.h>
#include <src/OtaRollback.h>

// ----- Hardware-Konfiguration -----
#define LED_PIN      2      // Status-LED
#define BTN_PIN      0      // BOOT-Taster, LOW-aktiv
#define OLED_SDA     33     // Leiste mit 3V3/5V (DevKitC)
#define OLED_SCL     32
#define OLED_ADDR    0x3C

// ----- Betrieb -----
static const char*    DEFAULT_HOSTNAME = "fuellstand-anzeige";
static const char*    AP_SSID          = "Fuellstand-Anzeige";
static const char*    AP_PASS          = "";                  // leer = offenes Portal-WLAN
static const char*    FW_VERSION       = "1.4.1";
static const uint32_t SHORT_PRESS_MS   = 700;
static const uint32_t LOST_PORTAL_MS   = 60000;
static const uint32_t PORTAL_TIMEOUT_S = 300;
static const uint16_t OTA_PORT         = 3232;
static const uint32_t MQTT_RETRY_MS    = 15000;
static const uint8_t  MAX_SENDERS      = 4;                   // Anzahl verwaltbarer Sender/Behälter
static const uint32_t OLED_PAGE_MS     = 5000;                // OLED: Wechsel zum nächsten Behälter
static const uint8_t  CFG_VERSION      = 2;                   // NVS-Layout (1 = ein Sender, 2 = mehrere)

// ----- Globale Konfiguration (NVS, Namespace "fill") -----
struct RxConfig {
	bool     oledFlip   = false;
	bool     mqttOn     = false;
	String   mqttHost;
	uint16_t mqttPort   = 1883;
	String   mqttUser;
	String   mqttPass;
	String   mqttBase   = "fuellstand";
} cfg;

// ----- Akku des Senders: Spannung für 0 % und 100 % (je Behälter einstellbar) -----
static const uint16_t BAT_EMPTY_MV_DEF = 3300;   // Li-Ion 3,7 V (1S): Entladeschluss unter Last
static const uint16_t BAT_FULL_MV_DEF  = 4200;   // Ladeschluss
static const uint16_t BAT_MV_MIN       = 2000;   // erlaubter Einstellbereich
static const uint16_t BAT_MV_MAX       = 6000;   // (Messbereich des Senders bis ca. 6 V)
static const uint16_t BAT_MV_SPAN_MIN  = 100;    // 100 % muss mindestens so viel über 0 % liegen

// ----- Behälter je Sender (NVS-Keys "t<n>...", n = Platz 0..MAX_SENDERS-1) -----
struct Tank {
	// Konfiguration
	bool       used       = false;    // Platz belegt (MAC gültig)
	bool       active     = true;     // aus = wie nicht vorhanden: kein Display, kein MQTT, Pakete werden verworfen
	uint8_t    mac[6]     = { 0 };    // STA-MAC des Senders
	String     name;                  // Anzeigename (OLED, Web, Home Assistant)
	String     uid;                   // eindeutige ID für MQTT/HA (bleibt beim Umbenennen gleich)
	uint8_t    shape      = 0;        // 0 = stehend (Zylinder/Quader, linear), 1 = liegender Zylinder
	uint16_t   distFull   = 100;      // Abstand Sensor -> Oberfläche bei VOLL (mm)
	uint16_t   distEmpty  = 1500;     // Abstand Sensor -> Boden/Oberfläche bei LEER (mm)
	float      capacityL  = 1000.0f;  // Inhalt bei voll (Liter)
	uint16_t   batEmptyMv = BAT_EMPTY_MV_DEF;   // Akkuspannung für 0 % (mV)
	uint16_t   batFullMv  = BAT_FULL_MV_DEF;    // Akkuspannung für 100 % (mV)
	// Messzustand (nicht gespeichert)
	bool       valid      = false;    // mind. ein Paket empfangen
	bool       levelOk    = false;    // Messung gültig und Geometrie plausibel
	FillPacket pkt        = {};
	int8_t     rssi       = 0;
	uint32_t   rxMillis   = 0;
	uint32_t   received   = 0;
	uint32_t   lost       = 0;
	float      levelPct   = 0;
	float      volumeL    = 0;
	float      levelCm    = 0;
	float      vbat       = 0;
	uint8_t    batPct     = 0;
	bool       senderOnline = false;
	bool       testMode   = false;    // Sender meldet Testmodus (FILL_FLAG_TESTMODE im letzten Paket)
	int8_t     testWant   = -1;       // angeforderter Testmodus (0/1), -1 = nichts ausstehend
	bool       otaMode    = false;    // Sender meldet OTA-Modus (FILL_FLAG_OTAMODE im letzten Paket)
	int8_t     otaWant    = -1;       // angeforderter OTA-Modus (0/1), -1 = nichts ausstehend
	uint16_t   intWant    = 0;        // angefordertes Messintervall (s), 0 = nichts ausstehend
};
Tank tanks[MAX_SENDERS];

Preferences        prefs;
ESPWiFiManagerAP   wifiManagerAP(LED_PIN, BTN_PIN);
ESPWiFiManagerSTA  wifiManagerSTA;
Adafruit_SH1106G   oled(128, 64, &Wire, -1);
WiFiClient         mqttNet;
PubSubClient       mqtt(mqttNet);

bool     oledOk         = false;
bool     otaReady       = false;
uint32_t disconnectedAt = 0;
String   devId;                    // Geräte-ID des Empfängers (MQTT-Client-ID)
char     mqttHostBuf[64] = { 0 };  // PubSubClient speichert nur den Zeiger
bool     cfgChanged     = false;   // aus Web-Handler gesetzt, im loop() verarbeitet
bool     discoveryDirty = false;   // HA-Discovery erneut senden (Sender neu/umbenannt)
bool     statePubDirty  = false;   // Zustände erneut senden (Geometrie geändert)
bool     availDirty     = false;   // Verfügbarkeit je Behälter erneut senden (ein/aus geändert)
String   removedUids[MAX_SENDERS]; // entfernte Behälter: Discovery in HA löschen

// Web-Handler (AsyncTCP-Task) und loop() greifen auf dieselben Daten zu -> gemeinsamer Mutex.
// MQTT wird nur aus dem loop() bedient.
SemaphoreHandle_t dataMtx = nullptr;
struct DataLock {
	DataLock()  { xSemaphoreTakeRecursive(dataMtx, portMAX_DELAY); }
	~DataLock() { xSemaphoreGiveRecursive(dataMtx); }
};

// ESP-NOW-Übergabe aus dem WLAN-Task (Ringpuffer, mehrere Sender können kurz nacheinander senden)
struct RxItem { FillPacket p; uint8_t mac[6]; int8_t rssi; };
static const uint8_t RXQ_LEN = 8;
portMUX_TYPE  rxMux = portMUX_INITIALIZER_UNLOCKED;
RxItem        rxq[RXQ_LEN];
volatile uint8_t rxHead = 0, rxTail = 0;
bool          espNowUp = false;
wifi_mode_t   espNowMode = WIFI_MODE_NULL;

// Befehle an Sender (Test-/OTA-Modus ein/aus, Messintervall): Der Empfangs-Callback vergleicht den Zustand im
// Datenpaket mit dem angeforderten und reicht den Befehl an cmdTask weiter, der ihn sofort sendet (der Sender
// lauscht nach jedem Paket nur FILL_CMD_WAIT_MS). Je Paket geht ein Befehl raus (Testmodus, OTA-Modus, Intervall); die
// Bestätigung ist wieder ein Datenpaket, auf das der nächste Befehl folgt.
// cmdSlots spiegelt Tank::testWant/otaWant/intWant für den WLAN-Task (unter rxMux).
struct CmdSlot { uint8_t mac[6]; int8_t want; int8_t otaWant; uint16_t intWant; };
struct CmdItem { uint8_t mac[6]; uint8_t cmd; uint16_t value; };
CmdSlot       cmdSlots[MAX_SENDERS];
QueueHandle_t cmdQueue = nullptr;

// Unbekannter Sender (nicht eingetragen) – wird in der Weboberfläche zur Übernahme angeboten
bool     unkValid = false;
uint8_t  unkMac[6];
uint32_t unkAt    = 0;
uint16_t unkDist  = 0;

// Kopplung: Anfrage des Senders aus dem WLAN-Task, Antwort im loop()
static const uint32_t PAIR_WINDOW_MS = 120000;
static const uint8_t  BCAST_MAC[6]   = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
volatile bool pairReqNew   = false;
uint8_t       pairReqMac[6];
uint32_t      pairUntil    = 0;       // millis() bis Kopplungsmodus aktiv; 0 = aus
String        pairJustDone;           // MAC des zuletzt gekoppelten Senders (Weboberfläche)
bool          pairFull     = false;   // Kopplung abgelehnt: alle Plätze belegt

// OLED-Blättern
uint8_t  oledPage   = 0;
uint32_t oledPageAt = 0;

// ============================================================ Hilfsfunktionen

String fillMacStr(const uint8_t* m) {
	char buf[18];
	snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
	return String(buf);
}

bool fillParseMac(const String& s, uint8_t* out) {
	unsigned int b[6];
	if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
	for (int i = 0; i < 6; i++) { if (b[i] > 255) return false; out[i] = (uint8_t)b[i]; }
	return true;
}

// HA-/MQTT-ID eines Senders aus seiner MAC
String senderUid(const uint8_t* m) {
	char id[24];
	snprintf(id, sizeof(id), "fuellstand_%02x%02x%02x", m[3], m[4], m[5]);
	return String(id);
}

// NVS-Key je Platz, z. B. "t0dfull" (max. 15 Zeichen)
String tkey(uint8_t i, const char* k) {
	char b[16];
	snprintf(b, sizeof(b), "t%u%s", (unsigned)i, k);
	return String(b);
}

// getString() meldet fehlende Keys als Fehler im Log -> vorher prüfen
String prefsStr(const String& key, const char* def = "") {
	return prefs.isKey(key.c_str()) ? prefs.getString(key.c_str(), def) : String(def);
}

// Sender-Akku -> Prozent über die Entladekurve eines Li-Ion 3,7 V (1S)
// (Stützpunkte, dazwischen linear interpoliert; Spannung unter Last, daher grober Richtwert).
// Die Kurve gilt für 3,30 V = 0 % … 4,20 V = 100 %; abweichend eingestellte Grenzen (emptyMv/fullMv) werden
// linear auf diesen Bereich abgebildet, die Form der Kurve bleibt erhalten.
struct BatPoint { uint16_t mV; uint8_t pct; };
static const BatPoint BAT_CURVE[] = {
	{ 3300,   0 }, { 3500,   5 }, { 3600,  10 }, { 3700,  25 }, { 3750,  40 },
	{ 3800,  50 }, { 3900,  65 }, { 4000,  78 }, { 4100,  90 }, { 4200, 100 },
};
uint8_t batteryPercent(uint16_t raw, uint16_t emptyMv, uint16_t fullMv) {
	const size_t n = sizeof(BAT_CURVE) / sizeof(BAT_CURVE[0]);
	if (raw <= emptyMv) return 0;
	if (raw >= fullMv)  return 100;
	const int32_t c0 = BAT_CURVE[0].mV, c1 = BAT_CURVE[n - 1].mV;
	const uint16_t mV = (uint16_t)(c0 + (int32_t)(raw - emptyMv) * (c1 - c0) / (int32_t)(fullMv - emptyMv));
	if (mV <= BAT_CURVE[0].mV)     return 0;
	if (mV >= BAT_CURVE[n - 1].mV) return 100;
	for (size_t i = 1; i < n; i++) {
		if (mV <= BAT_CURVE[i].mV) {
			const BatPoint& a = BAT_CURVE[i - 1];
			const BatPoint& b = BAT_CURVE[i];
			return (uint8_t)(a.pct + (uint32_t)(mV - a.mV) * (b.pct - a.pct) / (b.mV - a.mV));
		}
	}
	return 100;
}

// Datenalter in Sekunden
uint32_t dataAgeS(const Tank& t) { return t.valid ? (millis() - t.rxMillis) / 1000 : 0; }

// Sender gilt als offline nach 3 verpassten Intervallen (+ 60 s Reserve)
bool senderStale(const Tank& t) {
	if (!t.valid) return true;
	const uint32_t interval = t.pkt.interval_s ? t.pkt.interval_s : 600;
	return dataAgeS(t) > interval * 3UL + 60UL;
}

uint8_t usedCount() {
	uint8_t n = 0;
	for (uint8_t i = 0; i < MAX_SENDERS; i++) if (tanks[i].used) n++;
	return n;
}

// Behälter, die auf dem OLED erscheinen (eingetragen und aktiv)
bool    oledShown(const Tank& t) { return t.used && t.active; }
uint8_t shownCount() {
	uint8_t n = 0;
	for (uint8_t i = 0; i < MAX_SENDERS; i++) if (oledShown(tanks[i])) n++;
	return n;
}

int findTank(const uint8_t* mac) {
	for (uint8_t i = 0; i < MAX_SENDERS; i++)
		if (tanks[i].used && memcmp(tanks[i].mac, mac, 6) == 0) return i;
	return -1;
}

void resetRuntime(Tank& t) {
	t.valid = false; t.levelOk = false; t.pkt = {}; t.rssi = 0; t.rxMillis = 0;
	t.received = 0; t.lost = 0; t.levelPct = 0; t.volumeL = 0; t.levelCm = 0;
	t.vbat = 0; t.batPct = 0; t.senderOnline = false; t.testMode = false; t.testWant = -1;
	t.otaMode = false; t.otaWant = -1; t.intWant = 0;
}

// ============================================================ Konfiguration (NVS)

void saveTank(uint8_t i) {
	const Tank& t = tanks[i];
	prefs.begin("fill", false);
	prefs.putString(tkey(i, "mac").c_str(),    t.used ? fillMacStr(t.mac) : String(""));
	prefs.putString(tkey(i, "name").c_str(),   t.name);
	prefs.putBool  (tkey(i, "act").c_str(),    t.active);
	prefs.putString(tkey(i, "uid").c_str(),    t.uid);
	prefs.putUChar (tkey(i, "shape").c_str(),  t.shape);
	prefs.putUShort(tkey(i, "dfull").c_str(),  t.distFull);
	prefs.putUShort(tkey(i, "dempty").c_str(), t.distEmpty);
	prefs.putFloat (tkey(i, "cap").c_str(),    t.capacityL);
	prefs.putUShort(tkey(i, "bemp").c_str(),   t.batEmptyMv);
	prefs.putUShort(tkey(i, "bful").c_str(),   t.batFullMv);
	prefs.end();
}

void saveConfig() {
	prefs.begin("fill", false);
	prefs.putBool("flip", cfg.oledFlip);
	prefs.putBool("mon", cfg.mqttOn);
	prefs.putString("mhost", cfg.mqttHost);
	prefs.putUShort("mport", cfg.mqttPort);
	prefs.putString("muser", cfg.mqttUser);
	prefs.putString("mpass", cfg.mqttPass);
	prefs.putString("mbase", cfg.mqttBase);
	prefs.end();
}

// Version 1 (ein Sender) -> Version 2: alte Behälterdaten werden Platz 1.
// Die alte Geräte-ID (devId) bleibt als uid erhalten, damit Home Assistant die bisherigen Entitäten
// samt Verlauf weiterführt. War kein Sender-Filter gesetzt, wird Platz 1 mit dem ersten Sender belegt,
// der sich meldet (die gespeicherte Geometrie bleibt erhalten).
void migrateV1() {
	const bool legacy = prefs.isKey("smac") || prefs.isKey("dempty") || prefs.isKey("shape");
	if (legacy) {
		uint8_t mac[6];
		const bool macOk = fillParseMac(prefsStr("smac"), mac);
		prefs.putString("t0mac",    macOk ? fillMacStr(mac) : String(""));
		prefs.putString("t0name",   "Füllstand");
		prefs.putString("t0uid",    devId);
		prefs.putUChar ("t0shape",  prefs.getUChar("shape", 0));
		prefs.putUShort("t0dfull",  prefs.getUShort("dfull", 100));
		prefs.putUShort("t0dempty", prefs.getUShort("dempty", 1500));
		prefs.putFloat ("t0cap",    prefs.getFloat("cap", 1000.0f));
		for (const char* k : { "smac", "shape", "dfull", "dempty", "cap" }) if (prefs.isKey(k)) prefs.remove(k);
		Serial.println("Konfiguration auf mehrere Sender umgestellt (bisheriger Behälter = Platz 1)");
	}
	prefs.putUChar("ver", CFG_VERSION);
}

void loadConfig() {
	prefs.begin("fill", false);
	if (prefs.getUChar("ver", 1) < CFG_VERSION) migrateV1();

	cfg.oledFlip  = prefs.getBool("flip", false);
	cfg.mqttOn    = prefs.getBool("mon", false);
	cfg.mqttHost  = prefsStr("mhost");
	cfg.mqttPort  = prefs.getUShort("mport", 1883);
	cfg.mqttUser  = prefsStr("muser");
	cfg.mqttPass  = prefsStr("mpass");
	cfg.mqttBase  = prefsStr("mbase", "fuellstand");

	for (uint8_t i = 0; i < MAX_SENDERS; i++) {
		Tank& t = tanks[i];
		t.used      = fillParseMac(prefsStr(tkey(i, "mac")), t.mac);
		t.name      = prefsStr(tkey(i, "name"));
		t.active    = prefs.getBool(tkey(i, "act").c_str(), true);
		t.uid       = prefsStr(tkey(i, "uid"));
		t.shape     = prefs.getUChar(tkey(i, "shape").c_str(), 0);
		t.distFull  = prefs.getUShort(tkey(i, "dfull").c_str(), 100);
		t.distEmpty = prefs.getUShort(tkey(i, "dempty").c_str(), 1500);
		t.capacityL = prefs.getFloat(tkey(i, "cap").c_str(), 1000.0f);
		t.batEmptyMv = constrain(prefs.getUShort(tkey(i, "bemp").c_str(), BAT_EMPTY_MV_DEF), BAT_MV_MIN, BAT_MV_MAX);
		t.batFullMv  = constrain(prefs.getUShort(tkey(i, "bful").c_str(), BAT_FULL_MV_DEF),  BAT_MV_MIN, BAT_MV_MAX);
		if (t.batFullMv < t.batEmptyMv + BAT_MV_SPAN_MIN) { t.batEmptyMv = BAT_EMPTY_MV_DEF; t.batFullMv = BAT_FULL_MV_DEF; }
		if (t.used && !t.uid.length())  t.uid  = senderUid(t.mac);
		if (t.used && !t.name.length()) t.name = "Behälter " + String(i + 1);
	}
	prefs.end();
}

// Sender eintragen; liefert den Platz oder -1 (alle belegt). Bereits bekannte Sender behalten ihren Platz.
int addTank(const uint8_t* mac) {
	int slot = findTank(mac);
	if (slot >= 0) return slot;
	for (uint8_t i = 0; i < MAX_SENDERS && slot < 0; i++)      // vorbereiteter Platz (Migration) zuerst
		if (!tanks[i].used && tanks[i].uid.length()) slot = i;
	for (uint8_t i = 0; i < MAX_SENDERS && slot < 0; i++)
		if (!tanks[i].used) slot = i;
	if (slot < 0) return -1;

	Tank& t = tanks[slot];
	if (!t.uid.length()) {                                       // neuer Platz: Standardwerte
		t = Tank();
		t.name = "Behälter " + String(slot + 1);
		t.uid  = senderUid(mac);
	}
	resetRuntime(t);
	memcpy(t.mac, mac, 6);
	t.used = true;
	saveTank(slot);
	if (unkValid && memcmp(unkMac, mac, 6) == 0) unkValid = false;
	discoveryDirty = true;
	Serial.printf("Sender %s als \"%s\" eingetragen (Platz %d)\n", fillMacStr(mac).c_str(), t.name.c_str(), slot + 1);
	return slot;
}

void removeTank(uint8_t i) {
	if (i >= MAX_SENDERS || !tanks[i].used) return;
	Serial.printf("Sender %s (\"%s\") entfernt\n", fillMacStr(tanks[i].mac).c_str(), tanks[i].name.c_str());
	removedUids[i] = tanks[i].uid;      // Discovery-Einträge beim nächsten MQTT-Durchlauf löschen
	tanks[i] = Tank();
	saveTank(i);
}

// ============================================================ Berechnung

void computeLevel(Tank& t) {
	t.vbat   = t.pkt.vbat_mV / 1000.0f;
	t.batPct = batteryPercent(t.pkt.vbat_mV, t.batEmptyMv, t.batFullMv);

	t.levelOk = (t.pkt.status == FILL_OK) && (t.distEmpty > t.distFull);
	if (!t.levelOk) return;

	const float H   = (float)(t.distEmpty - t.distFull);            // nutzbare Höhe
	float       lvl = (float)t.distEmpty - (float)t.pkt.distance_mm; // Pegel über "leer"
	lvl = constrain(lvl, 0.0f, H);
	const float h = lvl / H;                                        // 0..1

	float frac = h;
	if (t.shape == 1) {                                             // liegender Zylinder: Kreissegment
		const float th = 2.0f * acosf(1.0f - 2.0f * h);
		frac = (th - sinf(th)) / (2.0f * (float)M_PI);
	}
	t.levelCm  = lvl / 10.0f;
	t.levelPct = frac * 100.0f;
	t.volumeL  = frac * t.capacityL;
}

// ============================================================ MQTT / Home Assistant

String mqttTopic(const char* sub) { return cfg.mqttBase + "/" + sub; }
String tankTopic(const Tank& t, const char* sub) { return cfg.mqttBase + "/" + t.uid + "/" + sub; }

void mqttApplyConfig() {
	if (mqtt.connected()) {
		mqtt.publish(mqttTopic("status").c_str(), "offline", true);
		mqtt.disconnect();
	}
	strlcpy(mqttHostBuf, cfg.mqttHost.c_str(), sizeof(mqttHostBuf));
	mqtt.setServer(mqttHostBuf, cfg.mqttPort);
}

static const HaEntity HA_ENTITIES[] = {
	{ "sensor", "level",     "Füllstand",        "%",     nullptr,           "{{ value_json.level_pct }}",   "mdi:water-percent",          false },
	{ "sensor", "volume",    "Inhalt",           "L",     "volume_storage",  "{{ value_json.volume_l }}",    nullptr,                      false },
	{ "sensor", "height",    "Pegel",            "cm",    "distance",        "{{ value_json.level_cm }}",    "mdi:arrow-expand-vertical",  false },
	{ "sensor", "battery",   "Akku",             "%",     "battery",         "{{ value_json.battery_pct }}", nullptr,                      false },
	{ "sensor", "battery_v", "Akkuspannung",     "V",     "voltage",         "{{ value_json.battery_v }}",   nullptr,                      true  },
	{ "sensor", "distance",  "Sensorabstand",    "mm",    "distance",        "{{ value_json.distance_mm }}", nullptr,                      true  },
	{ "sensor", "rssi",      "Funksignal",       "dBm",   "signal_strength", "{{ value_json.rssi }}",        nullptr,                      true  },
	{ "sensor", "lost",      "Verlorene Pakete", nullptr, nullptr,           "{{ value_json.lost }}",        "mdi:package-variant-remove", true  },
	{ "binary_sensor", "sender", "Sender",       nullptr, "connectivity",    nullptr,                        nullptr,                      true  },
};

String discoveryTopic(const String& uid, const HaEntity& e) {
	return String("homeassistant/") + e.component + "/" + uid + "/" + e.key + "/config";
}

void mqttDiscoveryTank(const Tank& t) {
	for (const HaEntity& e : HA_ENTITIES) {
		JsonDocument d;
		d["name"]               = e.name;
		d["unique_id"]          = t.uid + "_" + e.key;
		d["object_id"]          = t.uid + "_" + e.key;
		// verfügbar nur, wenn die Anzeige online UND der Behälter eingeschaltet ist
		JsonArray av = d["availability"].to<JsonArray>();
		av.add<JsonObject>()["topic"] = mqttTopic("status");
		av.add<JsonObject>()["topic"] = tankTopic(t, "avail");
		d["availability_mode"] = "all";
		if (strcmp(e.component, "binary_sensor") == 0) {
			d["state_topic"]  = tankTopic(t, "sender");
			d["payload_on"]   = "online";
			d["payload_off"]  = "offline";
		} else {
			d["state_topic"]    = tankTopic(t, "state");
			d["value_template"] = e.valueTpl;
			d["state_class"]    = "measurement";
		}
		if (e.unit)       d["unit_of_measurement"] = e.unit;
		if (e.devClass)   d["device_class"]        = e.devClass;
		if (e.icon)       d["icon"]                = e.icon;
		if (e.diagnostic) d["entity_category"]     = "diagnostic";

		JsonObject dev = d["device"].to<JsonObject>();
		dev["identifiers"].to<JsonArray>().add(t.uid);
		dev["name"]           = t.name;
		dev["manufacturer"]   = "R.Wisser";
		dev["model"]          = "ESP32 FillLevel (VL53L1X)";
		dev["sw_version"]     = FW_VERSION;
		if (WiFi.isConnected()) dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/fill";

		String payload;
		serializeJson(d, payload);
		mqtt.publish(discoveryTopic(t.uid, e).c_str(), payload.c_str(), true);
	}
}

// Discovery und gespeicherte Werte eines entfernten Behälters löschen
void mqttRemoveTank(const String& uid) {
	for (const HaEntity& e : HA_ENTITIES) mqtt.publish(discoveryTopic(uid, e).c_str(), "", true);
	mqtt.publish((cfg.mqttBase + "/" + uid + "/state").c_str(),  "", true);
	mqtt.publish((cfg.mqttBase + "/" + uid + "/sender").c_str(), "", true);
	mqtt.publish((cfg.mqttBase + "/" + uid + "/avail").c_str(),  "", true);
}

// Behälter ein/aus -> Verfügbarkeit in Home Assistant
void mqttPublishAvail(const Tank& t) {
	if (mqtt.connected() && t.used) mqtt.publish(tankTopic(t, "avail").c_str(), t.active ? "online" : "offline", true);
}

void mqttPublishState(const Tank& t) {
	if (!mqtt.connected() || !t.used || !t.active || !t.valid) return;
	JsonDocument d;
	if (t.levelOk) {
		d["level_pct"] = roundf(t.levelPct * 10) / 10;
		d["volume_l"]  = roundf(t.volumeL * 10) / 10;
		d["level_cm"]  = roundf(t.levelCm * 10) / 10;
	} else {
		d["level_pct"] = nullptr;
		d["volume_l"]  = nullptr;
		d["level_cm"]  = nullptr;
	}
	d["distance_mm"] = t.pkt.distance_mm;
	d["spread_mm"]   = t.pkt.spread_mm;
	d["battery_v"]   = roundf(t.vbat * 100) / 100;
	d["battery_pct"] = t.batPct;
	d["rssi"]        = t.rssi;
	d["seq"]         = t.pkt.seq;
	d["lost"]        = t.lost;
	d["test_mode"]   = t.testMode;
	d["ota_mode"]    = t.otaMode;
	d["status"]      = t.pkt.status == FILL_OK ? "ok" : (t.pkt.status == FILL_SENSOR_ERROR ? "sensor_error" : "no_valid");
	String payload;
	serializeJson(d, payload);
	mqtt.publish(tankTopic(t, "state").c_str(), payload.c_str(), true);
}

void mqttPublishSender(const Tank& t) {
	if (mqtt.connected() && t.used && t.active) mqtt.publish(tankTopic(t, "sender").c_str(), t.senderOnline ? "online" : "offline", true);
}

// Ausstehende Discovery-Änderungen senden (Aufruf mit Daten-Lock, MQTT verbunden)
void mqttFlushPending(bool all) {
	for (uint8_t i = 0; i < MAX_SENDERS; i++)
		if (removedUids[i].length()) { mqttRemoveTank(removedUids[i]); removedUids[i] = ""; }
	if (all || discoveryDirty) {
		discoveryDirty = false;
		statePubDirty  = false;
		availDirty     = false;
		for (const Tank& t : tanks) if (t.used) { mqttDiscoveryTank(t); mqttPublishAvail(t); mqttPublishSender(t); mqttPublishState(t); }
	}
	if (availDirty) {
		availDirty = false;
		for (const Tank& t : tanks) if (t.used) { mqttPublishAvail(t); mqttPublishSender(t); mqttPublishState(t); }
	}
	if (statePubDirty) {
		statePubDirty = false;
		for (const Tank& t : tanks) mqttPublishState(t);
	}
}

void mqttLoop() {
	String user, pass;
	uint16_t port = 0;
	String willTopic;
	{
		DataLock l;
		if (!cfg.mqttOn || cfg.mqttHost.isEmpty()) {
			if (mqtt.connected()) mqtt.disconnect();
			return;
		}
		if (!WiFi.isConnected()) return;
		if (mqtt.connected()) {
			mqtt.loop();
			mqttFlushPending(false);
			return;
		}
		user = cfg.mqttUser;
		pass = cfg.mqttPass;
		port = cfg.mqttPort;
		willTopic = mqttTopic("status");
	}

	static uint32_t lastTry = 0;
	if (lastTry && millis() - lastTry < MQTT_RETRY_MS) return;
	lastTry = millis();

	// Verbindungsaufbau blockiert ggf. einige Sekunden -> ohne Daten-Lock (Weboberfläche bleibt bedienbar)
	Serial.printf("MQTT: verbinde mit %s:%u ...\n", mqttHostBuf, (unsigned)port);
	const bool ok = mqtt.connect(devId.c_str(),
		user.length() ? user.c_str() : nullptr,
		pass.length() ? pass.c_str() : nullptr,
		willTopic.c_str(), 0, true, "offline");
	if (!ok) { Serial.printf("MQTT: Fehler %d\n", mqtt.state()); return; }

	Serial.println("MQTT: verbunden");
	DataLock l;
	mqtt.publish(willTopic.c_str(), "online", true);
	// Topics der Version 1.0 (ein Sender, ohne uid im Pfad) leeren
	mqtt.publish(mqttTopic("state").c_str(),  "", true);
	mqtt.publish(mqttTopic("sender").c_str(), "", true);
	mqttFlushPending(true);
}

// ============================================================ ESP-NOW

void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
	if (len == (int)sizeof(FillPairPacket)) {
		FillPairPacket q;
		memcpy(&q, data, sizeof(q));
		if (q.magic != FILL_PAIR_MAGIC || q.version != FILL_PROTO_VERSION || q.type != FILL_PAIR_REQUEST) return;
		portENTER_CRITICAL(&rxMux);
		memcpy(pairReqMac, q.txMac, 6);      // STA-MAC aus der Nutzlast (src_addr kann die AP-MAC sein)
		pairReqNew = true;
		portEXIT_CRITICAL(&rxMux);
		return;
	}
	if (len != (int)sizeof(FillPacket)) return;
	FillPacket p;
	memcpy(&p, data, sizeof(p));
	if (p.magic != FILL_PROTO_MAGIC || p.version != FILL_PROTO_VERSION) return;
	portENTER_CRITICAL(&rxMux);
	// Befehl für diesen Sender ausstehend? Der Sender lauscht jetzt kurz -> sofort an cmdTask übergeben
	int8_t   want    = -1;
	int8_t   otaWant = -1;
	uint16_t intWant = 0;
	for (uint8_t i = 0; i < MAX_SENDERS; i++)
		if ((cmdSlots[i].want >= 0 || cmdSlots[i].otaWant >= 0 || cmdSlots[i].intWant) &&
			memcmp(cmdSlots[i].mac, info->src_addr, 6) == 0) {
			want = cmdSlots[i].want; otaWant = cmdSlots[i].otaWant; intWant = cmdSlots[i].intWant; break;
		}
	const uint8_t next = (uint8_t)((rxHead + 1) % RXQ_LEN);
	if (next != rxTail) {                    // Puffer voll -> Paket verwerfen
		RxItem& it = rxq[rxHead];
		it.p = p;
		memcpy(it.mac, info->src_addr, 6);
		it.rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
		rxHead = next;
	}
	portEXIT_CRITICAL(&rxMux);
	if (!cmdQueue) return;
	CmdItem c;
	memcpy(c.mac, info->src_addr, 6);
	if (want >= 0 && (bool)want != ((p.status & FILL_FLAG_TESTMODE) != 0)) {
		c.cmd = FILL_CMD_TESTMODE; c.value = (uint16_t)want;
	} else if (otaWant >= 0 && (bool)otaWant != ((p.status & FILL_FLAG_OTAMODE) != 0)) {
		c.cmd = FILL_CMD_OTAMODE; c.value = (uint16_t)otaWant;
	} else if (intWant && intWant != p.interval_s) {
		c.cmd = FILL_CMD_INTERVAL; c.value = intWant;
	} else return;
	xQueueSend(cmdQueue, &c, 0);
}

// Sendet Befehle an Sender (Broadcast, txMac adressiert den Sender – wie die Kopplungsantwort)
void cmdTask(void*) {
	CmdItem c;
	for (;;) {
		if (xQueueReceive(cmdQueue, &c, portMAX_DELAY) != pdTRUE || !espNowUp) continue;
		FillCmdPacket q = {};
		q.magic   = FILL_CMD_MAGIC;
		q.version = FILL_PROTO_VERSION;
		q.cmd      = c.cmd;
		q.value    = (uint8_t)(c.value & 0xFF);
		q.value_hi = (uint8_t)(c.value >> 8);
		memcpy(q.txMac, c.mac, 6);
		esp_now_send(BCAST_MAC, (const uint8_t*)&q, sizeof(q));
	}
}

// Tank::testWant/otaWant/intWant -> cmdSlots (für den Empfangs-Callback); Aufruf mit Daten-Lock
void syncCmdSlots() {
	portENTER_CRITICAL(&rxMux);
	for (uint8_t i = 0; i < MAX_SENDERS; i++) {
		const Tank& t = tanks[i];
		const bool on = t.used && t.active;
		memcpy(cmdSlots[i].mac, t.mac, 6);
		cmdSlots[i].want    = on ? t.testWant : -1;
		cmdSlots[i].otaWant = on ? t.otaWant : -1;
		cmdSlots[i].intWant = on ? t.intWant : 0;
	}
	portEXIT_CRITICAL(&rxMux);
}

void espNowStart() {
	if (espNowUp) esp_now_deinit();
	espNowUp = (esp_now_init() == ESP_OK);
	if (espNowUp) {
		esp_now_register_recv_cb(onEspNowRecv);
		esp_now_peer_info_t peer = {};          // Broadcast-Peer für die Kopplungsantwort
		memcpy(peer.peer_addr, BCAST_MAC, 6);
		peer.channel = 0;
		peer.ifidx   = WIFI_IF_STA;
		peer.encrypt = false;
		esp_now_add_peer(&peer);
	}
	espNowMode = WiFi.getMode();
	Serial.printf("ESP-NOW %s | Kanal %ld | MAC %s\n", espNowUp ? "aktiv" : "FEHLER",
		(long)WiFi.channel(), WiFi.macAddress().c_str());
}

// ESP-NOW nach WLAN-Moduswechsel (Portal auf/zu) neu initialisieren; WLAN nie ganz aus lassen
void espNowWatch() {
	const wifi_mode_t m = WiFi.getMode();
	if (m == WIFI_MODE_NULL) {
		WiFi.mode(WIFI_STA);           // z. B. Portal ohne Zugangsdaten geschlossen
		return;                        // im nächsten Durchlauf neu starten
	}
	if (m == WIFI_MODE_AP) {
		// Der Sender adressiert die STA-MAC -> im Portal-Betrieb STA-Interface zusätzlich aktivieren
		WiFi.mode(WIFI_AP_STA);
		return;
	}
	if (m != espNowMode || !espNowUp) espNowStart();
}

bool pairingActive() { return pairUntil && (int32_t)(pairUntil - millis()) > 0; }

void pairingStart() {
	pairUntil    = millis() + PAIR_WINDOW_MS;
	pairJustDone = "";
	pairFull     = false;
	Serial.println("Kopplungsmodus aktiv (120 s)");
}
void pairingStop()  { pairUntil = 0; }

// Kopplungsanfrage eines Senders beantworten (Kopplungsmodus oder bereits eingetragener Sender).
// Im Kopplungsmodus wird ein neuer Sender auf dem nächsten freien Platz eingetragen.
void handlePairRequest() {
	if (!pairReqNew) return;
	uint8_t mac[6];
	portENTER_CRITICAL(&rxMux);
	memcpy(mac, pairReqMac, 6); pairReqNew = false;
	portEXIT_CRITICAL(&rxMux);

	const bool known = findTank(mac) >= 0;
	if (!pairingActive() && !known) return;
	if (!espNowUp) return;

	int slot = -1;
	if (!known) {
		slot = addTank(mac);
		if (slot < 0) {                    // keine Antwort -> Sender meldet "nicht gefunden"
			pairFull = true;
			Serial.printf("Kopplung abgelehnt: alle %u Plätze belegt (%s)\n", (unsigned)MAX_SENDERS, fillMacStr(mac).c_str());
			return;
		}
	}

	FillPairPacket rsp = {};
	rsp.magic   = FILL_PAIR_MAGIC;
	rsp.version = FILL_PROTO_VERSION;
	rsp.type    = FILL_PAIR_RESPONSE;
	rsp.channel = (uint8_t)WiFi.channel();
	esp_read_mac(rsp.rxMac, ESP_MAC_WIFI_STA);
	memcpy(rsp.txMac, mac, 6);
	esp_now_send(BCAST_MAC, (const uint8_t*)&rsp, sizeof(rsp));

	if (!known) {
		pairJustDone = fillMacStr(mac);
		pairingStop();
		Serial.printf("Sender gekoppelt: %s (Platz %d, Kanal %u)\n", pairJustDone.c_str(), slot + 1, rsp.channel);
	}
}

void handleRx() {
	while (true) {
		RxItem it;
		portENTER_CRITICAL(&rxMux);
		const bool have = (rxTail != rxHead);
		if (have) { it = rxq[rxTail]; rxTail = (uint8_t)((rxTail + 1) % RXQ_LEN); }
		portEXIT_CRITICAL(&rxMux);
		if (!have) return;

		int i = findTank(it.mac);
		if (i < 0 && usedCount() == 0) i = addTank(it.mac);   // noch kein Sender eingetragen: ersten übernehmen
		if (i < 0) {                        // unbekannter Sender -> in der Weboberfläche zur Übernahme anbieten
			if (!unkValid || memcmp(unkMac, it.mac, 6) != 0)
				Serial.printf("Unbekannter Sender %s (nicht eingetragen)\n", fillMacStr(it.mac).c_str());
			memcpy(unkMac, it.mac, 6);
			unkAt    = millis();
			unkDist  = it.p.distance_mm;
			unkValid = true;
			continue;
		}

		Tank& t = tanks[i];
		if (!t.active) continue;            // ausgeschaltet: wie nicht vorhanden -> verwerfen
		// Wiederholung desselben Pakets (ACK ging verloren) ignorieren
		if (t.valid && it.p.seq == t.pkt.seq) continue;
		if (t.valid && it.p.seq > t.pkt.seq + 1) t.lost += it.p.seq - t.pkt.seq - 1;
		const bool tm = (it.p.status & FILL_FLAG_TESTMODE) != 0;
		const bool om = (it.p.status & FILL_FLAG_OTAMODE) != 0;
		it.p.status &= FILL_STATUS_MASK;    // ab hier nur noch FillStatus
		if (tm != t.testMode)
			Serial.printf("Sender \"%s\": Testmodus %s\n", t.name.c_str(), tm ? "EIN" : "aus");
		t.testMode = tm;
		if (t.testWant >= 0 && (bool)t.testWant == tm) t.testWant = -1;   // Befehl ausgeführt
		if (om != t.otaMode)
			Serial.printf("Sender \"%s\": OTA-Modus %s\n", t.name.c_str(), om ? "EIN" : "aus");
		t.otaMode = om;
		if (t.otaWant >= 0 && (bool)t.otaWant == om) t.otaWant = -1;
		if (t.intWant && it.p.interval_s == t.intWant) {
			Serial.printf("Sender \"%s\": Messintervall %u s übernommen\n", t.name.c_str(), it.p.interval_s);
			t.intWant = 0;
		}
		t.pkt      = it.p;
		t.rssi     = it.rssi;
		t.rxMillis = millis();
		t.received++;
		t.valid    = true;
		computeLevel(t);

		Serial.printf("RX %s \"%s\" seq=%lu dist=%u mm status=%u akku=%.2f V rssi=%d -> %.1f %% / %.0f L\n",
			fillMacStr(it.mac).c_str(), t.name.c_str(), (unsigned long)it.p.seq, it.p.distance_mm, it.p.status,
			t.vbat, it.rssi, t.levelPct, t.volumeL);

		mqttPublishState(t);
	}
}

// ============================================================ OLED (SH1106 128x64)

// UTF-8 -> Codepage 437 (Umlaute des eingebauten Fonts), andere Sonderzeichen -> '?'
String oledText(const String& s) {
	String out;
	for (size_t i = 0; i < s.length(); i++) {
		const uint8_t c = (uint8_t)s[i];
		if (c < 0x80) { out += (char)c; continue; }
		if (c == 0xC3 && i + 1 < s.length()) {
			const uint8_t d = (uint8_t)s[++i];
			char m = '?';
			switch (d) {
				case 0xA4: m = (char)0x84; break;   // ä
				case 0xB6: m = (char)0x94; break;   // ö
				case 0xBC: m = (char)0x81; break;   // ü
				case 0x84: m = (char)0x8E; break;   // Ä
				case 0x96: m = (char)0x99; break;   // Ö
				case 0x9C: m = (char)0x9A; break;   // Ü
				case 0x9F: m = (char)0xE1; break;   // ß
			}
			out += m;
			continue;
		}
		while (i + 1 < s.length() && ((uint8_t)s[i + 1] & 0xC0) == 0x80) i++;   // Folgebytes überspringen
		out += '?';
	}
	return out;
}

void oledSetup() {
	oledOk = oled.begin(OLED_ADDR, true);
	if (!oledOk) {
		Serial.printf("⚠️ OLED nicht gefunden (0x%02X, SDA %d / SCL %d) – I²C-Scan:", OLED_ADDR, OLED_SDA, OLED_SCL);
		uint8_t found = 0;
		for (uint8_t a = 1; a < 127; a++) {
			Wire.beginTransmission(a);
			if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
		}
		Serial.println(found ? "" : " kein Gerät (Verdrahtung/Versorgung prüfen)");
		return;
	}
	oled.cp437(true);
	oled.setRotation(cfg.oledFlip ? 2 : 0);
	oled.clearDisplay();
	oled.setTextColor(SH110X_WHITE);
	oled.setTextSize(1);
	oled.setCursor(0, 0);  oled.print("F\x81llstand-Anzeige");
	oled.setCursor(0, 20); oled.print("Verbinde WLAN ...");
	oled.setCursor(0, 54); oled.print("v"); oled.print(FW_VERSION);
	oled.display();
}

// Statuszeile: Kopplung / Portal haben Vorrang
bool oledGlobalStatus() {
	if (pairingActive())                { oled.print("Kopplung ...");   return true; }
	if (wifiManagerAP.isPortalActive()) { oled.print("AP 192.168.4.1"); return true; }
	return false;
}

void oledDraw() {
	if (!oledOk) return;
	oled.clearDisplay();
	oled.setTextColor(SH110X_WHITE);
	oled.setTextSize(1);

	const uint8_t n = shownCount();
	if (n == 0) {
		const bool none = usedCount() == 0;
		oled.setCursor(0, 0);  oled.print("F\x81llstand");
		oled.setCursor(0, 16); oled.print(none ? "Kein Sender" : "Alle Beh\x84lter");
		oled.setCursor(0, 26); oled.print(none ? "eingetragen." : "ausgeblendet.");
		oled.setCursor(0, 40); oled.print(none ? "Kopplung: /fill" : "Einschalten: /fill");
		oled.setCursor(0, 56);
		if (!oledGlobalStatus()) {
			if (WiFi.isConnected()) oled.print(WiFi.localIP());
			else                    oled.print("kein WLAN");
		}
		oled.display();
		return;
	}

	// Seite wählen: alle OLED_PAGE_MS zum nächsten aktiven Behälter
	const uint32_t now = millis();
	if (oledPage >= MAX_SENDERS || !oledShown(tanks[oledPage]) || (n > 1 && now - oledPageAt >= OLED_PAGE_MS)) {
		for (uint8_t k = 1; k <= MAX_SENDERS; k++) {
			const uint8_t c = (uint8_t)((oledPage + k) % MAX_SENDERS);
			if (oledShown(tanks[c])) { oledPage = c; break; }
		}
		oledPageAt = now;
	}
	const Tank& t = tanks[oledPage];
	uint8_t ord = 0;                                    // laufende Nummer der Seite (1..n)
	for (uint8_t i = 0; i <= oledPage; i++) if (oledShown(tanks[i])) ord++;

	// Kopfzeile: Name (+ Seitenzähler)
	String title = oledText(t.name);
	const unsigned maxChars = n > 1 ? 12 : 16;
	if (title.length() > maxChars) title = title.substring(0, maxChars);
	oled.setCursor(0, 0);
	oled.print(title);
	if (n > 1) {
		char pg[8];
		snprintf(pg, sizeof(pg), "%u/%u", (unsigned)ord, (unsigned)n);
		oled.setCursor(100 - 6 * (int)strlen(pg), 0);
		oled.print(pg);
	}

	// Tanksymbol rechts
	const int tx = 101, ty = 0, tw = 27, th = 64;
	oled.drawRoundRect(tx, ty, tw, th, 4, SH110X_WHITE);
	if (t.valid && t.levelOk) {
		const int fh = (int)lroundf((th - 4) * t.levelPct / 100.0f);
		if (fh > 0) oled.fillRect(tx + 2, ty + th - 2 - fh, tw - 4, fh, SH110X_WHITE);
	}

	// Großer Wert
	oled.setTextSize(3);
	oled.setCursor(0, 12);
	if (!t.valid)                oled.print("--");
	else if (!t.levelOk)         oled.print("ERR");
	else { oled.print((int)lroundf(t.levelPct)); oled.print('%'); }

	oled.setTextSize(1);
	char line[24];
	// Zeile 1: Inhalt + Pegel
	oled.setCursor(0, 38);
	if (t.valid && t.levelOk) {
		snprintf(line, sizeof(line), "%.0f L  %.0f cm", t.volumeL, t.levelCm);
		oled.print(line);
	} else if (t.valid) {
		oled.print(t.pkt.status == FILL_SENSOR_ERROR ? "Sensorfehler" : "Keine Messung");
	} else {
		oled.print("Warte auf Sender");
	}

	// Zeile 2: Akku des Senders
	oled.setCursor(0, 47);
	if (t.valid) {
		snprintf(line, sizeof(line), "Akku %.2fV %u%%", t.vbat, t.batPct);
		oled.print(line);
	}

	// Zeile 3: Status (Kopplung / Portal / Sender offline / Datenalter / IP)
	oled.setCursor(0, 56);
	if (oledGlobalStatus()) {
		// bereits ausgegeben
	} else if (t.valid && senderStale(t)) {
		oled.print("Sender offline!");
	} else if (t.valid) {
		const uint32_t a = dataAgeS(t);
		if (a < 60) snprintf(line, sizeof(line), "vor %lus", (unsigned long)a);
		else        snprintf(line, sizeof(line), "vor %lu min", (unsigned long)(a / 60));
		oled.print(line);
	} else if (WiFi.isConnected()) {
		oled.print(WiFi.localIP());
	} else {
		oled.print("kein WLAN");
	}
	oled.display();
}

// ============================================================ Web-API

void sendJson(AsyncWebServerRequest* r, JsonDocument& doc, int code) {
	String out;
	serializeJson(doc, out);
	r->send(code, "application/json", out);
}

void sendError(AsyncWebServerRequest* r, const String& msg, int code = 400) {
	JsonDocument d;
	d["ok"] = false; d["error"] = msg;
	sendJson(r, d, code);
}

// Platz aus Parameter "idx" (0-basiert); -1 = ungültig oder nicht belegt
int argTank(AsyncWebServerRequest* r) {
	if (!r->hasArg("idx")) return -1;
	const long i = r->arg("idx").toInt();
	if (i < 0 || i >= MAX_SENDERS || !tanks[i].used) return -1;
	return (int)i;
}

void handleState(AsyncWebServerRequest* r) {
	DataLock l;
	JsonDocument d;
	d["own_mac"]     = WiFi.macAddress();
	d["channel"]     = WiFi.channel();
	d["mqtt"]        = mqtt.connected();
	d["pairing"]     = pairingActive();
	d["pair_left_s"] = pairingActive() ? (pairUntil - millis()) / 1000 : 0;
	d["paired"]      = pairJustDone;
	d["pair_full"]   = pairFull;
	d["max"]         = MAX_SENDERS;
	d["fw"]          = FW_VERSION;
	d["ota_pending"] = otaPendingVerify();          // neue Firmware noch nicht bestätigt
	d["ota_info"]    = otaRollbackInfo();           // "" oder Hinweis auf Rollback
	if (unkValid) {
		JsonObject u = d["unknown"].to<JsonObject>();
		u["mac"]         = fillMacStr(unkMac);
		u["age_s"]       = (millis() - unkAt) / 1000;
		u["distance_mm"] = unkDist;
	}
	JsonArray arr = d["senders"].to<JsonArray>();
	for (uint8_t i = 0; i < MAX_SENDERS; i++) {
		const Tank& t = tanks[i];
		if (!t.used) continue;
		JsonObject o = arr.add<JsonObject>();
		o["idx"]         = i;
		o["name"]        = t.name;
		o["mac"]         = fillMacStr(t.mac);
		o["active"]      = t.active;
		o["valid"]       = t.valid;
		o["level_ok"]    = t.levelOk;
		o["level_pct"]   = t.levelPct;
		o["volume_l"]    = t.volumeL;
		o["level_cm"]    = t.levelCm;
		o["distance_mm"] = t.pkt.distance_mm;
		o["spread_mm"]   = t.pkt.spread_mm;
		o["status"]      = t.pkt.status;
		o["battery_v"]   = t.vbat;
		o["battery_pct"] = t.batPct;
		o["rssi"]        = t.rssi;
		o["seq"]         = t.pkt.seq;
		o["received"]    = t.received;
		o["lost"]        = t.lost;
		o["age_s"]       = dataAgeS(t);
		o["interval_s"]  = t.pkt.interval_s;
		o["online"]      = t.valid && !senderStale(t);
		o["test_mode"]   = t.testMode;
		if (t.testWant >= 0) o["test_req"] = t.testWant == 1;   // angefordert, vom Sender noch nicht bestätigt
		else                 o["test_req"] = nullptr;
		o["ota_mode"]    = t.otaMode;
		if (t.otaWant >= 0) o["ota_req"] = t.otaWant == 1;      // angefordert, vom Sender noch nicht bestätigt
		else                o["ota_req"] = nullptr;
		if (t.intWant) o["interval_req"] = t.intWant;           // angefordertes Messintervall (s), noch nicht bestätigt
		else           o["interval_req"] = nullptr;
	}
	sendJson(r, d, 200);
}

void handleGetConfig(AsyncWebServerRequest* r) {
	DataLock l;
	JsonDocument d;
	d["oled_flip"]     = cfg.oledFlip;
	d["mqtt_on"]       = cfg.mqttOn;
	d["mqtt_host"]     = cfg.mqttHost;
	d["mqtt_port"]     = cfg.mqttPort;
	d["mqtt_user"]     = cfg.mqttUser;
	d["mqtt_has_pass"] = cfg.mqttPass.length() > 0;
	d["mqtt_base"]     = cfg.mqttBase;
	JsonArray arr = d["senders"].to<JsonArray>();
	for (uint8_t i = 0; i < MAX_SENDERS; i++) {
		const Tank& t = tanks[i];
		if (!t.used) continue;
		JsonObject o = arr.add<JsonObject>();
		o["idx"]        = i;
		o["name"]       = t.name;
		o["mac"]        = fillMacStr(t.mac);
		o["uid"]        = t.uid;
		o["active"]     = t.active;
		o["shape"]      = t.shape;
		o["dist_full"]  = t.distFull;
		o["dist_empty"] = t.distEmpty;
		o["capacity_l"] = t.capacityL;
		o["bat_empty_mv"] = t.batEmptyMv;
		o["bat_full_mv"]  = t.batFullMv;
	}
	sendJson(r, d, 200);
}

// Globale Einstellungen: Display, MQTT
void handleSaveConfig(AsyncWebServerRequest* r) {
	DataLock l;
	String base = r->arg("mqtt_base"); base.trim();
	while (base.endsWith("/")) base.remove(base.length() - 1);
	if (!base.length()) base = "fuellstand";

	cfg.oledFlip  = r->hasArg("oled_flip");
	cfg.mqttOn    = r->hasArg("mqtt_on");
	cfg.mqttHost  = r->arg("mqtt_host"); cfg.mqttHost.trim();
	const long port = r->arg("mqtt_port").toInt();
	cfg.mqttPort  = (port > 0 && port <= 65535) ? (uint16_t)port : 1883;   // leer/ungültig -> Standardport statt 1
	cfg.mqttUser  = r->arg("mqtt_user");
	if (r->arg("mqtt_pass").length()) cfg.mqttPass = r->arg("mqtt_pass");   // leer = behalten
	if (r->hasArg("mqtt_clear_pass")) cfg.mqttPass = "";
	cfg.mqttBase  = base;
	saveConfig();
	cfgChanged = true;                // Display, MQTT im loop()
	JsonDocument d;
	d["ok"] = true;
	sendJson(r, d, 200);
}

// ============================================================ Werkseinstellung

static const uint32_t FACTORY_HOLD_MS = 10000;   // BOOT-Taster so lange halten → Werkseinstellung inkl. WLAN
static const uint32_t FACTORY_HINT_MS = 3000;    // ab hier Countdown im Display; Loslassen = Abbruch
uint8_t  factoryReq = 0;                         // aus Web-Handler: 1 = Konfiguration, 2 = inkl. WLAN
uint32_t factoryAt  = 0;                         // Zeitpunkt der Anforderung (Antwort erst ausliefern)

// Löscht die Konfiguration (NVS "fill"), optional auch die WLAN-Daten, entfernt die Behälter aus
// Home Assistant und startet neu. Kehrt nicht zurück.
void factoryReset(bool wifiToo) {
	Serial.printf("[RESET] Werkseinstellung: Konfiguration%s löschen\n", wifiToo ? " und WLAN" : "");
	if (oledOk) {
		oled.clearDisplay();
		oled.setCursor(0, 0);  oled.print("Werkseinstellung");
		oled.setCursor(0, 20); oled.print(wifiToo ? "Konfig. + WLAN" : "Konfiguration");
		oled.setCursor(0, 30); oled.print("wird gel\x94scht ...");
		oled.display();
	}
	{
		DataLock l;
		if (mqtt.connected()) {            // Behälter aus Home Assistant entfernen (retained Discovery leeren)
			for (const Tank& t : tanks) if (t.used && t.uid.length()) mqttRemoveTank(t.uid);
			mqtt.loop();
		}
	}
	delay(300);
	mqtt.disconnect();
	esp_now_deinit();
	prefs.begin("fill", false);
	prefs.clear();
	prefs.end();
	if (wifiToo) {
		eraseCredentials();                // /config.json (SSID, Passwort, feste IP)
		WiFi.disconnect(true, true);       // auch im WLAN-Treiber gespeicherte Zugangsdaten löschen
	}
	Serial.println("[RESET] Fertig – Neustart");
	Serial.flush();
	delay(500);
	ESP.restart();
}

// POST /api/fill/factory  confirm=1, wifi=0|1
void handleFactory(AsyncWebServerRequest* r) {
	if (r->arg("confirm") != "1") { sendError(r, "Bestätigung fehlt"); return; }
	factoryReq = r->arg("wifi") == "1" ? 2 : 1;
	factoryAt  = millis();
	JsonDocument d;
	d["ok"] = true;
	sendJson(r, d, 200);               // Ausführung im loop(), damit die Antwort noch ankommt
}

void setTankActive(Tank& t, bool on);

// Behälter eines Senders: POST /api/fill/tank  idx, name, active (Checkbox), shape, dist_full, dist_empty, capacity_l
void handleSaveTank(AsyncWebServerRequest* r) {
	DataLock l;
	const int i = argTank(r);
	if (i < 0) { sendError(r, "Unbekannter Sender"); return; }
	const long dFull  = r->arg("dist_full").toInt();
	const long dEmpty = r->arg("dist_empty").toInt();
	if (dFull < 0 || dEmpty <= dFull || dEmpty > 8000) {
		sendError(r, "Abstand 'leer' muss größer als 'voll' sein (max. 8000 mm)");
		return;
	}
	String name = r->arg("name"); name.trim();
	if (name.length() > 32) {          // nicht mitten in einem UTF-8-Zeichen abschneiden (sonst ungültiges JSON für HA)
		unsigned n = 32;
		while (n && ((uint8_t)name[n] & 0xC0) == 0x80) n--;
		name = name.substring(0, n);
	}
	if (!name.length()) name = "Behälter " + String(i + 1);

	Tank& t = tanks[i];
	// Akkuspannung für 0 % / 100 % (optional – fehlt das Feld, bleibt der bisherige Wert)
	const long bEmpty = r->hasArg("bat_empty_mv") ? r->arg("bat_empty_mv").toInt() : t.batEmptyMv;
	const long bFull  = r->hasArg("bat_full_mv")  ? r->arg("bat_full_mv").toInt()  : t.batFullMv;
	if (bEmpty < BAT_MV_MIN || bFull > BAT_MV_MAX || bFull < bEmpty + BAT_MV_SPAN_MIN) {
		sendError(r, "Akkuspannung: 100 % muss mindestens 0,1 V über 0 % liegen (Bereich 2,0 … 6,0 V)");
		return;
	}
	if (name != t.name) discoveryDirty = true;     // HA-Gerätename aktualisieren
	t.name      = name;
	setTankActive(t, r->hasArg("active"));
	t.shape     = r->arg("shape").toInt() == 1 ? 1 : 0;
	t.distFull  = (uint16_t)dFull;
	t.distEmpty = (uint16_t)dEmpty;
	t.capacityL = max(0.0f, r->arg("capacity_l").toFloat());
	t.batEmptyMv = (uint16_t)bEmpty;
	t.batFullMv  = (uint16_t)bFull;
	saveTank(i);
	if (t.valid) { computeLevel(t); statePubDirty = true; }
	JsonDocument d;
	d["ok"] = true;
	sendJson(r, d, 200);
}

// Behälter ein/aus. Aus = wie nicht vorhanden: Messwerte werden verworfen, HA meldet "nicht verfügbar".
void setTankActive(Tank& t, bool on) {
	if (t.active == on) return;
	t.active = on;
	resetRuntime(t);                  // keine veralteten Werte, kein "verlorene Pakete"-Sprung beim Einschalten
	availDirty = true;
	Serial.printf("Behälter \"%s\" %s\n", t.name.c_str(), on ? "eingeschaltet" : "ausgeschaltet");
}

// Ein/aus ohne übrige Felder: POST /api/fill/active  idx, active=0|1
void handleActive(AsyncWebServerRequest* r) {
	DataLock l;
	const int i = argTank(r);
	if (i < 0) { sendError(r, "Unbekannter Sender"); return; }
	setTankActive(tanks[i], r->arg("active") == "1");
	saveTank(i);
	JsonDocument d;
	d["ok"]     = true;
	d["active"] = tanks[i].active;
	sendJson(r, d, 200);
}

// Testmodus des Senders anfordern: POST /api/fill/testmode  idx, enabled=0|1
// Der Befehl geht beim nächsten Datenpaket des Senders raus (im Deep Sleep also erst nach dem Messintervall);
// bis zur Bestätigung meldet /api/fill/state "test_req". Gilt nur für eingeschaltete Behälter.
void handleTestMode(AsyncWebServerRequest* r) {
	DataLock l;
	const int i = argTank(r);
	if (i < 0) { sendError(r, "Unbekannter Sender"); return; }
	Tank& t = tanks[i];
	if (!t.active) { sendError(r, "Behälter ist ausgeschaltet – Pakete dieses Senders werden verworfen"); return; }
	const bool on = r->arg("enabled") == "1";
	t.testWant = (t.valid && t.testMode == on) ? -1 : (on ? 1 : 0);   // Zustand schon erreicht -> nichts senden
	Serial.printf("Testmodus %s für \"%s\" angefordert%s\n", on ? "EIN" : "AUS", t.name.c_str(),
		t.testWant < 0 ? " (bereits aktiv)" : " – wird beim nächsten Paket gesendet");
	JsonDocument d;
	d["ok"]        = true;
	d["test_mode"] = t.testMode;
	if (t.testWant >= 0) d["test_req"] = t.testWant == 1;
	else                 d["test_req"] = nullptr;
	sendJson(r, d, 200);
}

// OTA-Modus des Senders anfordern: POST /api/fill/otamode  idx, enabled=0|1
// Sender bleibt wach und verbindet sich mit dem Heim-WLAN (Update über /update). Ablauf wie beim Testmodus.
void handleOtaMode(AsyncWebServerRequest* r) {
	DataLock l;
	const int i = argTank(r);
	if (i < 0) { sendError(r, "Unbekannter Sender"); return; }
	Tank& t = tanks[i];
	if (!t.active) { sendError(r, "Behälter ist ausgeschaltet – Pakete dieses Senders werden verworfen"); return; }
	const bool on = r->arg("enabled") == "1";
	t.otaWant = (t.valid && t.otaMode == on) ? -1 : (on ? 1 : 0);    // Zustand schon erreicht -> nichts senden
	Serial.printf("OTA-Modus %s für \"%s\" angefordert%s\n", on ? "EIN" : "AUS", t.name.c_str(),
		t.otaWant < 0 ? " (bereits aktiv)" : " – wird beim nächsten Paket gesendet");
	JsonDocument d;
	d["ok"]       = true;
	d["ota_mode"] = t.otaMode;
	if (t.otaWant >= 0) d["ota_req"] = t.otaWant == 1;
	else                d["ota_req"] = nullptr;
	sendJson(r, d, 200);
}

// Messintervall des Senders ändern: POST /api/fill/interval  idx, interval_s=30..65000
// Wie beim Testmodus geht der Befehl mit dem nächsten Datenpaket an den Sender, also auch aus dem Deep Sleep
// (dann spätestens nach dem bisherigen Intervall). Bis zur Bestätigung meldet /api/fill/state "interval_req".
void handleInterval(AsyncWebServerRequest* r) {
	DataLock l;
	const int i = argTank(r);
	if (i < 0) { sendError(r, "Unbekannter Sender"); return; }
	Tank& t = tanks[i];
	if (!t.active) { sendError(r, "Behälter ist ausgeschaltet – Pakete dieses Senders werden verworfen"); return; }
	const long s = r->arg("interval_s").toInt();
	if (s < FILL_INTERVAL_MIN_S || s > FILL_INTERVAL_MAX_S) {
		sendError(r, "Messintervall muss zwischen 30 s und " + String(FILL_INTERVAL_MAX_S / 60) + " min liegen");
		return;
	}
	t.intWant = (t.valid && t.pkt.interval_s == s) ? 0 : (uint16_t)s;   // schon eingestellt -> nichts senden
	Serial.printf("Messintervall %ld s für \"%s\" angefordert%s\n", s, t.name.c_str(),
		t.intWant ? " – wird beim nächsten Paket gesendet" : " (bereits eingestellt)");
	JsonDocument d;
	d["ok"]         = true;
	d["interval_s"] = t.pkt.interval_s;
	if (t.intWant) d["interval_req"] = t.intWant;
	else           d["interval_req"] = nullptr;
	sendJson(r, d, 200);
}

// Kalibrierung: einen Abstand (manuell eingegeben oder letzter Messwert) als VOLL bzw. LEER speichern
//   POST /api/fill/calibrate  idx=<Platz> target=full|empty  [distance_mm=<mm>]
//   ohne distance_mm wird der zuletzt empfangene gültige Messwert dieses Senders verwendet
void handleCalibrate(AsyncWebServerRequest* r) {
	DataLock l;
	const int i = argTank(r);
	if (i < 0) { sendError(r, "Unbekannter Sender"); return; }
	Tank& t = tanks[i];
	const String target = r->arg("target");
	if (target != "full" && target != "empty") { sendError(r, "Parameter target muss 'full' oder 'empty' sein"); return; }

	long mm;
	String src = r->arg("distance_mm"); src.trim();
	if (src.length()) {
		mm = src.toInt();
		if (mm <= 0 && src != "0") { sendError(r, "Ungültiger Messwert"); return; }
	} else {
		if (!t.valid || t.pkt.status != FILL_OK) {
			sendError(r, "Kein gültiger Messwert vom Sender vorhanden – Wert bitte manuell eingeben");
			return;
		}
		mm = t.pkt.distance_mm;
	}
	if (mm < 0 || mm > 8000) { sendError(r, "Messwert muss zwischen 0 und 8000 mm liegen"); return; }

	const bool full   = (target == "full");
	const long dFull  = full ? mm : t.distFull;
	const long dEmpty = full ? t.distEmpty : mm;
	if (dEmpty <= dFull) {
		sendError(r, full ? "Abstand VOLL muss kleiner als Abstand LEER sein (" + String(t.distEmpty) + " mm)"
		                  : "Abstand LEER muss größer als Abstand VOLL sein (" + String(t.distFull) + " mm)");
		return;
	}

	t.distFull  = (uint16_t)dFull;
	t.distEmpty = (uint16_t)dEmpty;
	saveTank(i);
	if (t.valid) { computeLevel(t); statePubDirty = true; }
	Serial.printf("Kalibrierung \"%s\": %s = %ld mm\n", t.name.c_str(), full ? "VOLL" : "LEER", mm);

	JsonDocument d;
	d["ok"]         = true;
	d["dist_full"]  = t.distFull;
	d["dist_empty"] = t.distEmpty;
	sendJson(r, d, 200);
}

// POST /api/fill/pair
//   action=start|stop      Kopplungsmodus (120 s) ein/aus
//   action=add mac=<MAC>   Sender von Hand eintragen (oder unbekannten Sender übernehmen)
//   action=remove idx=<n>  Sender entfernen (auch aus Home Assistant)
void handlePair(AsyncWebServerRequest* r) {
	DataLock l;
	JsonDocument d;
	const String a = r->arg("action");
	const String full = "Alle " + String(MAX_SENDERS) + " Plätze belegt – erst einen Sender entfernen";
	if (a == "stop") {
		pairingStop();
	} else if (a == "add") {
		uint8_t mac[6];
		if (!fillParseMac(r->arg("mac"), mac)) { sendError(r, "Ungültige MAC-Adresse (Format AA:BB:CC:DD:EE:FF)"); return; }
		const int slot = addTank(mac);
		if (slot < 0) { sendError(r, full); return; }
		d["idx"] = slot;
	} else if (a == "remove") {
		const int i = argTank(r);
		if (i < 0) { sendError(r, "Unbekannter Sender"); return; }
		removeTank(i);
	} else {
		if (usedCount() >= MAX_SENDERS) { sendError(r, full); return; }
		pairingStart();
	}
	d["ok"] = true;
	sendJson(r, d, 200);
}

void registerFillRoutes(AsyncWebServer& srv) {
	srv.on("/api/fill/pair", HTTP_POST, handlePair);
	srv.on("/fill", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", RECEIVER_PAGE); });
	srv.on("/api/fill/state",     HTTP_GET,  handleState);
	srv.on("/api/fill/config",    HTTP_GET,  handleGetConfig);
	srv.on("/api/fill/config",    HTTP_POST, handleSaveConfig);
	srv.on("/api/fill/tank",      HTTP_POST, handleSaveTank);
	srv.on("/api/fill/active",    HTTP_POST, handleActive);
	srv.on("/api/fill/calibrate", HTTP_POST, handleCalibrate);
	srv.on("/api/fill/testmode",  HTTP_POST, handleTestMode);
	srv.on("/api/fill/otamode",   HTTP_POST, handleOtaMode);
	srv.on("/api/fill/interval",  HTTP_POST, handleInterval);
	srv.on("/api/fill/factory",   HTTP_POST, handleFactory);
}

// ============================================================ WiFiManager (wie Basisprojekt)

void ensureOtaReady() {
	if (otaReady) return;
	String h = WiFi.getHostname();
	if (!h.length()) h = DEFAULT_HOSTNAME;
	ensureArduinoOta(h, OTA_PORT, "");
	otaReady = true;
}

void startPortalNow(const char* reason) {
	if (wifiManagerAP.isPortalActive()) return;
	Serial.printf("Starte Captive-Portal (%s)...\n", reason);
	wifiManagerSTA.stopSTA();
	wifiManagerAP.openPortal();
	ensureOtaReady();
}

// ============================================================ Arduino

void setup() {
	Serial.begin(115200);
	delay(200);
	Serial.printf("ESP32_FillLevel_Receiver v%s (WiFiManager v%s)\n", FW_VERSION, AWM_VERSION);

	dataMtx = xSemaphoreCreateRecursiveMutex();

	uint8_t mac[6];
	esp_read_mac(mac, ESP_MAC_WIFI_STA);
	devId = senderUid(mac);            // vor loadConfig(): wird bei der Migration als uid übernommen

	loadConfig();
	Wire.begin(OLED_SDA, OLED_SCL);
	oledSetup();

	if (!checkOtaAndDataPartitions(true))
		Serial.println("⚠️ Partition-Layout unzureichend (2 OTA-Slots + LittleFS nötig).");
	otaRollbackBegin();                    // neue Firmware nach OTA erst nach Selbsttest bestätigen
	ElegantOTA.onEnd([](bool ok) {         // fehlerhafter Upload: Boot-Partition bleibt unverändert
		if (ok) Serial.println("[OTA] Upload OK – Neustart, danach Selbsttest");
		else    Serial.printf("[OTA] ❌ Upload fehlgeschlagen (%s) – bisherige Firmware bleibt aktiv\n", Update.errorString());
	});

	mqtt.setBufferSize(1024);
	mqttApplyConfig();

	// Eigene Routen nur im Heim-WLAN (STA); das AP-Portal dient ausschließlich der WLAN-Konfiguration
	registerFillRoutes(wifiManagerSTA.server());

	wifiManagerAP.setHostname(DEFAULT_HOSTNAME);
	wifiManagerAP.setAPCredentials(AP_SSID, AP_PASS);
	wifiManagerAP.setFallbackPolicy(ESPWiFiManagerAP::FallbackPolicy::ON_FAIL);
	wifiManagerAP.begin();
	if (wifiManagerAP.hasCredentials()) wifiManagerAP.setPortalTimeout(PORTAL_TIMEOUT_S);
	wifiManagerAP.run();

	if (wifiManagerAP.isConnected()) {
		Serial.printf("WLAN verbunden: http://%s/fill  (http://%s.local/fill)\n",
			WiFi.localIP().toString().c_str(), DEFAULT_HOSTNAME);
		wifiManagerSTA.setHostname(WiFi.getHostname());
		wifiManagerSTA.startSTA();
	} else if (!wifiManagerAP.isPortalActive()) {
		startPortalNow("connect failed");
	}
	ensureOtaReady();
	cmdQueue = xQueueCreate(4, sizeof(CmdItem));
	xTaskCreate(cmdTask, "fillCmd", 3072, nullptr, 5, nullptr);
	espNowStart();
	Serial.printf(">>> MAC dieser Anzeige (für manuelle Eingabe am Sender): %s\n", WiFi.macAddress().c_str());
	for (uint8_t i = 0; i < MAX_SENDERS; i++)
		if (tanks[i].used) Serial.printf("    Platz %u: %s \"%s\"%s\n", (unsigned)(i + 1), fillMacStr(tanks[i].mac).c_str(),
			tanks[i].name.c_str(), tanks[i].active ? "" : " (ausgeschaltet)");
	oledDraw();
}

void loop() {
	const uint32_t now = millis();

	wifiManagerAP.update();
	wifiManagerSTA.handleClient();
	handleArduinoOta();
	ElegantOTA.loop();

	const bool portal    = wifiManagerAP.isPortalActive();
	const bool connected = wifiManagerAP.isConnected();
	// Selbsttest nach OTA: im Heim-WLAN und Webserver (inkl. /update) läuft → erneutes Update möglich
	otaRollbackLoop(connected && !portal && wifiManagerSTA.isRunning());

	// BOOT-Taster: 0,7–3 s → Portal öffnen; ≥10 s halten → Werkseinstellung inkl. WLAN (3–10 s loslassen = Abbruch)
	static bool     btnPrev = true;
	static uint32_t btnAt   = 0;
	const bool btnNow = digitalRead(BTN_PIN);
	if (btnPrev && !btnNow) btnAt = now;
	else if (!btnPrev && btnNow) {
		const uint32_t held = now - btnAt;
		if (held >= SHORT_PRESS_MS && held < FACTORY_HINT_MS) startPortalNow("button");
		else if (held >= FACTORY_HINT_MS) { Serial.println("[RESET] abgebrochen"); oledDraw(); }
	} else if (!btnNow) {
		const uint32_t held = now - btnAt;
		if (held >= FACTORY_HOLD_MS) factoryReset(true);
		static uint32_t lastHint = 0;
		if (held >= FACTORY_HINT_MS && oledOk && now - lastHint >= 200) {
			lastHint = now;
			oled.clearDisplay();
			oled.setCursor(0, 0);  oled.print("Werkseinstellung");
			oled.setCursor(0, 20); oled.printf("in %lu s ...", (unsigned long)((FACTORY_HOLD_MS - held + 999) / 1000));
			oled.setCursor(0, 40); oled.print("Loslassen = Abbruch");
			oled.display();
		}
	}
	const bool btnHint = !btnNow && now - btnAt >= FACTORY_HINT_MS;   // Countdown nicht überzeichnen
	btnPrev = btnNow;
	if (factoryReq && now - factoryAt > 1000) factoryReset(factoryReq == 2);

	if (!portal) wifiManagerAP.checkReconnect();
	if (connected) {
		disconnectedAt = 0;
		static uint32_t lastStaRetry = 0;
		if (!portal && !wifiManagerSTA.isRunning() && now - lastStaRetry > 3000) {
			lastStaRetry = now;
			wifiManagerSTA.startSTA();
		}
	} else if (!portal) {
		if (!disconnectedAt) {
			disconnectedAt = now ? now : 1;
			Serial.println("WLAN-Verbindung verloren → Reconnect");
		}
		if (now - disconnectedAt > LOST_PORTAL_MS) { disconnectedAt = 0; startPortalNow("connection lost"); }
	}

	// Füllstand
	espNowWatch();
	{
		DataLock l;
		handlePairRequest();
		handleRx();
		syncCmdSlots();

		if (cfgChanged) {
			cfgChanged = false;
			if (oledOk) oled.setRotation(cfg.oledFlip ? 2 : 0);
			mqttApplyConfig();             // verbindet im nächsten mqttLoop() neu (inkl. Discovery)
		}

		for (Tank& t : tanks) {
			if (!t.used || !t.active) continue;
			const bool online = t.valid && !senderStale(t);
			if (online != t.senderOnline) {
				t.senderOnline = online;
				Serial.printf("Sender \"%s\" %s\n", t.name.c_str(), online ? "online" : "offline");
				mqttPublishSender(t);
			}
		}

		static uint32_t lastDraw = 0;
		if (now - lastDraw >= 1000 && !btnHint) { lastDraw = now; oledDraw(); }
	}
	mqttLoop();

	static uint32_t lastStatus = 0;
	if (now - lastStatus > 30000) {
		lastStatus = now;
		DataLock l;
		uint32_t rx = 0, lost = 0;
		for (const Tank& t : tanks) if (t.used) { rx += t.received; lost += t.lost; }
		Serial.printf("WLAN %s | Kanal %ld | MQTT %s | Sender %u/%u | Pakete %lu (verloren %lu)\n",
			connected ? WiFi.localIP().toString().c_str() : (portal ? "Portal" : "getrennt"),
			(long)WiFi.channel(), mqtt.connected() ? "ok" : "-", (unsigned)usedCount(), (unsigned)MAX_SENDERS,
			(unsigned long)rx, (unsigned long)lost);
	}
}
