# Graph Report - ESP32_FillLevel_Receiver  (2026-11-12)

## Corpus Check
- 19 files · ~20,279 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 7 file(s) not represented in the graph (top: (none) 1, .xml 1, .ino 1)

## Summary
- 476 nodes · 906 edges · 32 communities (20 shown, 12 thin omitted)
- Extraction: 94% EXTRACTED · 6% INFERRED · 0% AMBIGUOUS · INFERRED: 56 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- WiFiManager AP State
- Project Docs & Build Setup
- WiFiManager STA Class
- Web Portal Pages
- OTA & Reconnect Logic
- Tank & Receiver Data Model
- ESP-NOW Receive & Tasks
- Tank HTTP API Handlers
- Portal HTTP Handlers
- Firmware Setup & Main Loop
- OLED Display & Status
- Home Assistant Entity
- Receiver MQTT Config
- Sender Pairing & Rx
- Config Persistence & Migration
- Fallback Policy
- .run
- Battery Curve Point
- Command Queue Item
- Command Slot
- Reconnect State
- Visual Studio Solution

## God Nodes (most connected - your core abstractions)
1. `ESPWiFiManagerAP` - 119 edges
2. `ESPWiFiManagerSTA` - 41 edges
3. `Tank` - 40 edges
4. `serveFileFromLittleFS()` - 12 edges
5. `loop()` - 12 edges
6. `sendJson()` - 12 edges
7. `fillMacStr()` - 11 edges
8. `loadConfigDoc()` - 11 edges
9. `handlePair()` - 11 edges
10. `HaEntity` - 10 edges

## Surprising Connections (you probably didn't know these)
- `ArduinoOTA (Port 3232) and OTA Scripts` --semantically_similar_to--> `ElegantOTA`  [INFERRED] [semantically similar]
  docs/DOKUMENTATION.md → README.md
- `Dokumentation ESP32_FillLevel_Receiver` --conceptually_related_to--> `ESP32_FillLevel_Receiver`  [INFERRED]
  docs/DOKUMENTATION.md → README.md
- `FillCmdPacket Test Mode Command Flow` --references--> `FillLevelProtocol.h Data Packet`  [EXTRACTED]
  docs/DOKUMENTATION.md → README.md
- `MQTT Topics and HA Discovery` --conceptually_related_to--> `MQTT Home Assistant Auto-Discovery`  [INFERRED]
  docs/DOKUMENTATION.md → README.md
- `Migration from Version 1.0` --conceptually_related_to--> `NVS Configuration (Namespace fill, ver 2, t<n> keys)`  [INFERRED]
  README.md → docs/DOKUMENTATION.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Pages implementing config erase danger zone** — data_settings_setupsta, data_settings_setupap, data_settingsap_settingsap_page, data_settingssta_settingssta_page, http_endpoint_erase [EXTRACTED 1.00]
- **Fill Level Data Pipeline (Sender -> ESP-NOW -> computeLevel -> OLED/MQTT)** — readme_esp32_filllevel_sender, docs_dokumentation_esp_now_packet, docs_dokumentation_computelevel, readme_sh1106_oled, docs_dokumentation_mqtt_topics [EXTRACTED 1.00]
- **STA network config editors via /api/sta/config** — data_settings_setupsta, data_settingssta_loadconfig, data_indexsta_indexsta_page, http_endpoint_api_sta_config, config_json_file [EXTRACTED 1.00]
- **Tank Volume Calculation Models** — docs_dokumentation_computelevel, docs_dokumentation_standing_tank, docs_dokumentation_horizontal_cylinder, docs_dokumentation_nvs_config [EXTRACTED 1.00]
- **Build and Flash Layout (partitions, LittleFS, OTA)** — board_visual_micro_overrides, board_partitions_csv, docs_dokumentation_partition_table, readme_littlefs, readme_elegantota [INFERRED 0.85]
- **WiFi provisioning flow (scan, save, result page)** — data_indexap_indexap_page, http_endpoint_scan, http_endpoint_save, data_success_success_page, data_error_error_page [INFERRED 0.85]

## Communities (32 total, 12 thin omitted)

### Community 0 - "WiFiManager AP State"
Cohesion: 0.04
Nodes (49): ESPWiFiManagerAP, allowButtonPortal, apClientCheck, apGW, apIP, apPASS, apServer, apSN (+41 more)

### Community 1 - "Project Docs & Build Setup"
Cohesion: 0.05
Nodes (46): mklittlefs Tool Path, partitions.csv Custom Partition Table, Visual Micro Build Property Overrides, graphify Knowledge Graph Usage Rules, ArduinoOTA (Port 3232) and OTA Scripts, BAT_CURVE Li-Ion Discharge Curve, computeLevel() Fill Level Calculation, Dokumentation ESP32_FillLevel_Receiver (+38 more)

### Community 2 - "WiFiManager STA Class"
Cohesion: 0.08
Nodes (17): ESPWiFiManagerSTA, dhcpEnabled, hostname, htmlPathPrefix, ntp1, ntp2, restartPending, restartReqAt (+9 more)

### Community 3 - "Web Portal Pages"
Cohesion: 0.10
Nodes (30): /config.json (persisted network config), error.html (Fehler beim Speichern), indexAP AP config prefill, indexAP dedupe(), indexAP.html (Captive-Portal WLAN setup), indexAP render(), indexAP scan(), indexSTA.html (Home-WLAN start page) (+22 more)

### Community 4 - "OTA & Reconnect Logic"
Cohesion: 0.11
Nodes (15): applyStaConfigFromWifiJson(), ensureArduinoOta(), eraseConfigJson(), eraseCredentials(), eraseJsonInDir(), isProtectedJson(), loadConfigDoc(), loadConfigJson() (+7 more)

### Community 5 - "Tank & Receiver Data Model"
Cohesion: 0.07
Nodes (35): mqttDiscoveryTank(), mqttFlushPending(), mqttPublishAvail(), mqttPublishSender(), mqttPublishState(), RxItem, mac, p (+27 more)

### Community 7 - "ESP-NOW Receive & Tasks"
Cohesion: 0.07
Nodes (3): DataLock, onEspNowRecv(), saveConfig()

### Community 8 - "Tank HTTP API Handlers"
Cohesion: 0.26
Nodes (16): argTank(), batteryPercent(), computeLevel(), handleActive(), handleCalibrate(), handleGetConfig(), handlePair(), handleSaveConfig() (+8 more)

### Community 11 - "Firmware Setup & Main Loop"
Cohesion: 0.21
Nodes (12): ensureOtaReady(), espNowStart(), espNowWatch(), loop(), mqttApplyConfig(), mqttLoop(), mqttTopic(), oledSetup() (+4 more)

### Community 13 - "OLED Display & Status"
Cohesion: 0.29
Nodes (10): dataAgeS(), handleState(), oledDraw(), oledGlobalStatus(), oledShown(), oledText(), pairingActive(), senderStale() (+2 more)

### Community 14 - "Home Assistant Entity"
Cohesion: 0.22
Nodes (9): HaEntity, component, devClass, diagnostic, icon, key, name, unit (+1 more)

### Community 16 - "Receiver MQTT Config"
Cohesion: 0.25
Nodes (8): RxConfig, mqttBase, mqttHost, mqttOn, mqttPass, mqttPort, mqttUser, oledFlip

### Community 18 - "Sender Pairing & Rx"
Cohesion: 0.48
Nodes (7): addTank(), fillMacStr(), findTank(), handlePairRequest(), handleRx(), pairingStop(), resetRuntime()

### Community 19 - "Config Persistence & Migration"
Cohesion: 0.39
Nodes (8): discoveryTopic(), fillParseMac(), loadConfig(), migrateV1(), mqttRemoveTank(), prefsStr(), senderUid(), tkey()

### Community 20 - "Fallback Policy"
Cohesion: 0.29
Nodes (6): FallbackPolicy, BUTTON_ONLY, NEVER, NO_CREDENTIALS_ONLY, ON_FAIL, SMART_RETRIES

### Community 21 - ".run"
Cohesion: 0.17
Nodes (7): LedPattern, BLINK_DOUBLE, BLINK_FAST, BLINK_SLOW, BLINK_TRIPLE, OFF, ON

### Community 22 - "Battery Curve Point"
Cohesion: 0.67
Nodes (3): BatPoint, mV, pct

### Community 23 - "Command Queue Item"
Cohesion: 0.67
Nodes (3): CmdItem, mac, value

### Community 24 - "Command Slot"
Cohesion: 0.67
Nodes (3): CmdSlot, mac, want

### Community 25 - "Reconnect State"
Cohesion: 0.67
Nodes (3): ReconnState, CONNECTING, IDLE

## Ambiguous Edges - Review These
- `settingsAP render()` → `SSID rendered via textContent (XSS guard)`  [AMBIGUOUS]
  data/settingsAP.html · relation: conceptually_related_to

## Knowledge Gaps
- **142 isolated node(s):** `allowButtonPortal`, `apClientCheck`, `apGW`, `apIP`, `apPASS` (+137 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 200 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **12 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ESPWiFiManagerAP` connect `WiFiManager AP State` to `OTA & Reconnect Logic`, `WiFiManager AP Configuration`, `Libraries & Headers`, `Portal HTTP Handlers`, `AP Portal Lifecycle & LED`, `WiFi Scan & LED Task`, `Fallback Policy`, `.run`, `Reconnect State`, `Async Web Server`?**
  _High betweenness centrality (0.263) - this node is a cross-community bridge._
- **Are the 8 inferred relationships involving `serveFileFromLittleFS()` (e.g. with `.handleCss()` and `.handleRoot()`) actually correct?**
  _`serveFileFromLittleFS()` has 8 INFERRED edges - model-reasoned connections that need verification._
- **What connects `allowButtonPortal`, `apClientCheck`, `apGW` to the rest of the system?**
  _142 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `WiFiManager AP State` be split into smaller, more focused modules?**
  _Cohesion score 0.04 - nodes in this community are weakly interconnected._
- **Should `Project Docs & Build Setup` be split into smaller, more focused modules?**
  _Cohesion score 0.04931972789115646 - nodes in this community are weakly interconnected._
- **Should `WiFiManager STA Class` be split into smaller, more focused modules?**
  _Cohesion score 0.07770582793709528 - nodes in this community are weakly interconnected._

### Low-confidence Hints
_AMBIGUOUS edges — the extractor was unsure. Verify before acting on these._

- **What is the exact relationship between `settingsAP render()` and `SSID rendered via textContent (XSS guard)`?**
  _Edge tagged AMBIGUOUS (relation: conceptually_related_to) - confidence is low._