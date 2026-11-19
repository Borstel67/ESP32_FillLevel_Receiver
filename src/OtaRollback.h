#pragma once
// ============================================================================
// OtaRollback – Rücksprung auf die vorherige Firmware, wenn ein OTA-Update scheitert
// Header-only: nur EINMAL einbinden (in der .ino). So braucht es keinen Eintrag im Build
// (Visual Micro übersetzt neue .cpp in src\ sonst nicht automatisch).
//
// Ablauf (Bootloader-Rollback, CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE ist im ESP32-Core aktiv):
//  - Nach einem OTA-Update startet die neue Firmware im Zustand PENDING_VERIFY.
//  - Normalerweise bestätigt der Arduino-Core sie sofort beim Start. verifyRollbackLater()
//    verschiebt das; die Firmware muss sich erst bewähren:
//      * otaRollbackLoop(healthy) bestätigt sie, wenn "healthy" OTA_CONFIRM_MS lang ununterbrochen gilt
//        (Empfänger: WLAN verbunden + Webserver läuft, also ein erneutes OTA-Update möglich).
//      * Gelingt das nicht innerhalb von OTA_VERIFY_TIMEOUT_MS → verwerfen, Neustart mit alter Firmware.
//      * Absturz/Watchdog-Reset vor der Bestätigung → der Bootloader springt selbst zurück.
//  - Ein abgebrochener/fehlerhafter Upload (ElegantOTA, ArduinoOTA) ändert die Boot-Partition gar nicht
//    erst; die laufende Firmware bleibt aktiv. Das wird nur protokolliert.
// ============================================================================
#include <Arduino.h>
#include <esp_ota_ops.h>

static const uint32_t OTA_CONFIRM_MS        = 30000;    // so lange muss "healthy" ununterbrochen gelten
static const uint32_t OTA_VERIFY_TIMEOUT_MS = 300000;   // spätestens dann Rollback

// Ersetzt die schwache Funktion im Arduino-Core (esp32-hal-misc.c): Bestätigung nicht beim Start,
// sondern erst in otaRollbackLoop().
extern "C" bool verifyRollbackLater() { return true; }

static bool     otaRbPending = false;
static uint32_t otaRbOkSince = 0;
static char     otaRbInfo[96] = "";

// in setup(): Zustand lesen und protokollieren
static void otaRollbackBegin() {
	const esp_partition_t* running = esp_ota_get_running_partition();
	esp_ota_img_states_t st;
	otaRbPending = running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
	if (otaRbPending)
		Serial.printf("[OTA] Neue Firmware in %s – wird nach %lu s fehlerfreiem Betrieb bestätigt, sonst Rollback (spätestens nach %lu s)\n",
			running->label, (unsigned long)(OTA_CONFIRM_MS / 1000), (unsigned long)(OTA_VERIFY_TIMEOUT_MS / 1000));

	const esp_partition_t* bad = esp_ota_get_last_invalid_partition();
	if (bad) {
		esp_app_desc_t d;
		if (esp_ota_get_partition_description(bad, &d) == ESP_OK)
			snprintf(otaRbInfo, sizeof(otaRbInfo), "Rollback: Firmware in %s (%s) wurde verworfen", bad->label, d.date);
		else
			snprintf(otaRbInfo, sizeof(otaRbInfo), "Rollback: Firmware in %s wurde verworfen", bad->label);
		Serial.printf("[OTA] ⚠️ %s – läuft wieder mit %s\n", otaRbInfo, running ? running->label : "?");
	}
}

// in loop(): healthy = Selbsttest erfüllt
static void otaRollbackLoop(bool healthy) {
	if (!otaRbPending) return;
	const uint32_t now = millis();
	if (!healthy) otaRbOkSince = 0;
	else if (!otaRbOkSince) otaRbOkSince = now ? now : 1;

	if (otaRbOkSince && now - otaRbOkSince >= OTA_CONFIRM_MS) {
		if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
			otaRbPending = false;
			otaRbInfo[0] = 0;
			Serial.println("[OTA] ✅ Neue Firmware bestätigt (Rollback aufgehoben)");
		}
	} else if (now >= OTA_VERIFY_TIMEOUT_MS) {
		Serial.println("[OTA] ❌ Neue Firmware nicht bestätigt → Rollback auf die vorherige Version, Neustart");
		Serial.flush();
		esp_ota_mark_app_invalid_rollback_and_reboot();   // kehrt nur zurück, wenn es keine gültige alte Firmware gibt
		otaRbPending = false;                              // dann bleibt die neue Firmware eben aktiv
		Serial.println("[OTA] Rollback nicht möglich (keine gültige vorherige Firmware)");
	}
}

static bool        otaPendingVerify() { return otaRbPending; }   // neue Firmware wartet noch auf Bestätigung
static const char* otaRollbackInfo()  { return otaRbInfo; }      // "" oder Hinweis auf eine verworfene Firmware
