// SPDX-License-Identifier: MIT
// Seite /fill der Füllstandsanzeige: mehrere Sender/Behälter – Live-Werte, Kalibrierung, Geometrie,
// Sender verwalten (Kopplung, Übernahme, Entfernen), Display und MQTT (nutzt WifiManager.css aus LittleFS)
#pragma once
#include <Arduino.h>

static const char RECEIVER_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="de">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover" />
<title>Füllstand</title>
<link rel="stylesheet" type="text/css" href="/WifiManager.css">
<style>
  .big{display:flex;align-items:center;gap:18px}
  .pct{font-size:46px;font-weight:700;line-height:1}
  .tank{width:56px;height:96px;border:2px solid var(--muted);border-radius:8px;position:relative;overflow:hidden;flex:none}
  .fill{position:absolute;left:0;right:0;bottom:0;background:var(--accent);transition:height .6s}
  .kv{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;font-size:14px;margin-top:12px}
  .kv span:nth-child(odd){color:var(--muted)}
  .row label{min-width:150px}
  .msg{margin-top:8px;font-size:14px;min-height:18px}
  .ok{color:var(--ok)} .bad{color:var(--bad)}
  .btns{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
  .chk{display:flex;align-items:center;gap:8px;margin:10px 0}
  .tabs{display:flex;flex-wrap:wrap;gap:8px}
  .tabs .btn{display:flex;flex-direction:column;align-items:flex-start;line-height:1.2}
  .tabs small{opacity:.75;font-size:12px}
  .list{display:grid;grid-template-columns:1fr auto auto;gap:8px 12px;align-items:center;font-size:14px;margin-top:10px}
  .hide{display:none}
  .dims{flex-wrap:wrap} .dims .input{flex:1 1 70px}
</style>
</head>
<body>
<div class="app">
  <div class="header">
    <div class="title">
      <div class="logo" aria-hidden="true">
        <svg viewBox="0 0 24 24" class="wifi-icon"><path d="M12 3c3 4 6 7.5 6 11a6 6 0 0 1-12 0c0-3.5 3-7 6-11z"/></svg>
      </div>
      <div><h1>Füllstand</h1><div class="hint" style="margin:0" id="sub">–</div></div>
    </div>
    <div class="actions"><a class="gear" href="/settings" title="WLAN-Einstellungen">⚙️</a></div>
  </div>

  <div class="card">
    <section class="sec">
      <h2>Behälter</h2>
      <div class="tabs" id="tabs"></div>
      <p class="hint hide" id="none">Noch kein Sender eingetragen. Unten unter „Sender verwalten“ die Kopplung starten.</p>
    </section>

    <div id="tankview" class="hide">
      <section class="sec">
        <div class="big">
          <div class="tank"><div class="fill" id="fill" style="height:0"></div></div>
          <div>
            <div class="pct" id="pct">--</div>
            <div id="vol" class="hint" style="font-size:18px;margin:4px 0 0">–</div>
          </div>
        </div>
        <div class="kv">
          <span>Status</span><span id="onl">–</span>
          <span>Pegel</span><span id="lvl">–</span>
          <span>Sensorabstand</span><span id="dist">–</span>
          <span>Sender-MAC</span><code id="smac">–</code>
          <span>Akku</span><span id="bat">–</span>
          <span>Funksignal</span><span id="rssi">–</span>
          <span>Letzte Daten</span><span id="age">–</span>
          <span>Pakete</span><span id="pk">–</span>
          <span>Testmodus</span><label class="chk" style="margin:0"><input type="checkbox" id="test_mode"> <span id="test_txt">–</span></label>
          <span>OTA-Modus</span><label class="chk" style="margin:0"><input type="checkbox" id="ota_mode"> <span id="ota_txt">–</span></label>
        </div>
        <div class="btns">
          <button class="btn" type="button" id="b_remove">Sender entfernen</button>
        </div>
        <div class="msg" id="smsg"></div>
      </section>

      <section class="sec">
        <h2>Kalibrieren</h2>
        <p class="hint">Abstand Sensor → Wasseroberfläche eintragen (z. B. mit dem Zollstock gemessen
          oder aus der Testmessung des Senders) und als VOLL oder LEER speichern.
          Leer lassen = zuletzt empfangenen Messwert dieses Senders verwenden.</p>
        <div class="row"><label for="cal_mm">Messwert (mm)</label>
          <input class="input" id="cal_mm" type="number" min="0" max="8000" placeholder="letzter Messwert">
          <button class="btn" type="button" id="b_cal_last" title="letzten Messwert einsetzen">↺</button></div>
        <div class="hint" id="cal_info">–</div>
        <div class="btns">
          <button class="btn primary" type="button" id="b_cal_full">Als VOLL speichern</button>
          <button class="btn primary" type="button" id="b_cal_empty">Als LEER speichern</button>
        </div>
        <div class="msg" id="cal_msg"></div>
      </section>

      <form id="ft">
        <section class="sec">
          <h2>Einstellungen dieses Behälters</h2>
          <div class="row"><label for="name">Name</label>
            <input class="input" id="name" name="name" maxlength="32" placeholder="z. B. Zisterne"></div>
          <label class="chk"><input type="checkbox" id="active" name="active"> Aktiv (aus = wie nicht vorhanden: kein Display, kein MQTT, Messwerte werden verworfen)</label>
          <div class="row"><label for="shape">Form</label>
            <select class="input" id="shape" name="shape">
              <option value="0">stehend (Tonne, IBC, Quader)</option>
              <option value="1">liegender Zylinder</option>
            </select></div>
          <div class="row"><label for="dist_full">Abstand bei VOLL (mm)</label>
            <input class="input" id="dist_full" name="dist_full" type="number" min="0" max="8000">
            <button class="btn" type="button" id="b_full" title="aktuellen Messwert übernehmen">↓</button></div>
          <div class="row"><label for="dist_empty">Abstand bei LEER (mm)</label>
            <input class="input" id="dist_empty" name="dist_empty" type="number" min="1" max="8000">
            <button class="btn" type="button" id="b_empty" title="aktuellen Messwert übernehmen">↓</button></div>
          <div class="row"><label for="capacity_l">Inhalt bei voll (L)</label>
            <input class="input" id="capacity_l" name="capacity_l" type="number" min="0" step="0.1"></div>
          <div class="row dims"><label>oder Maße (cm)</label>
            <input class="input" id="g_h" type="number" min="0" step="0.1" placeholder="Höhe">
            <input class="input" id="g_b" type="number" min="0" step="0.1" placeholder="Breite">
            <input class="input" id="g_t" type="number" min="0" step="0.1" placeholder="Tiefe/Länge">
            <input class="input" id="g_d" type="number" min="0" step="0.1" placeholder="Ø"></div>
          <div class="hint" id="g_info">Inhalt direkt eingeben oder aus Maßen berechnen lassen.</div>
          <div class="row"><label for="bat_empty_v">Akkuspannung bei 0 % (V)</label>
            <input class="input" id="bat_empty_v" type="number" min="2" max="6" step="0.01">
            <button class="btn" type="button" id="b_bemp" title="aktuelle Akkuspannung übernehmen">↓</button></div>
          <div class="row"><label for="bat_full_v">Akkuspannung bei 100 % (V)</label>
            <input class="input" id="bat_full_v" type="number" min="2" max="6" step="0.01">
            <button class="btn" type="button" id="b_bful" title="aktuelle Akkuspannung übernehmen">↓</button></div>
          <p class="hint">Abstände jeweils von der Sensor-Unterkante zur Wasseroberfläche. „↓“ übernimmt den zuletzt empfangenen Messwert.
            Akku: Standard 3,30 V / 4,20 V (Li-Ion 1S); 100 % muss mindestens 0,1 V über 0 % liegen.
            Der Name erscheint auf dem Display und als Gerät in Home Assistant.</p>
          <div class="btns">
            <button class="btn primary" type="submit">Behälter speichern</button>
          </div>
          <div class="msg" id="tmsg"></div>
        </section>
      </form>
    </div>

    <section class="sec">
      <h2>Sender verwalten</h2>
      <p class="hint">Neuen Sender koppeln: hier „Kopplung starten“ drücken, danach am Sender unter <code>/fill</code> „Empfänger suchen“.
        Der Sender findet Kanal und MAC selbst. Alternativ die MAC dieser Anzeige von Hand beim Sender eintragen –
        er erscheint dann unten als „unbekannter Sender“ und kann übernommen werden.</p>
      <div class="kv"><span>MAC Anzeige</span><code id="own">–</code>
        <span>Funkkanal</span><span id="ch">–</span>
        <span>Eingetragen</span><span id="cnt">–</span>
        <span>MQTT</span><span id="mq">–</span>
        <span>Firmware</span><span id="fw">–</span></div>
      <div class="list" id="slist"></div>
      <div class="btns">
        <button class="btn primary" type="button" id="b_pair">Kopplung starten (2 min)</button>
      </div>
      <div class="msg" id="pair_msg"></div>
      <div id="unk" class="hide">
        <p class="hint" id="unk_txt"></p>
        <div class="btns"><button class="btn" type="button" id="b_unk">Unbekannten Sender übernehmen</button></div>
      </div>
      <div class="row"><label for="add_mac">Sender-MAC von Hand</label>
        <input class="input" id="add_mac" placeholder="AA:BB:CC:DD:EE:FF">
        <button class="btn" type="button" id="b_add">Hinzufügen</button></div>
      <div class="msg" id="add_msg"></div>
    </section>

    <form id="f">
      <section class="sec">
        <h2>Display</h2>
        <label class="chk"><input type="checkbox" id="oled_flip" name="oled_flip"> Display um 180° drehen</label>
        <p class="hint">Das Display zeigt nur aktive Behälter und wechselt alle 5 s zum nächsten.</p>
      </section>

      <section class="sec">
        <h2>MQTT / Home Assistant</h2>
        <label class="chk"><input type="checkbox" id="mqtt_on" name="mqtt_on"> MQTT aktivieren (mit Home-Assistant-Discovery, ein Gerät je Behälter)</label>
        <div class="row"><label for="mqtt_host">Broker</label>
          <input class="input" id="mqtt_host" name="mqtt_host" placeholder="192.168.1.10 oder homeassistant.local"></div>
        <div class="row"><label for="mqtt_port">Port</label>
          <input class="input" id="mqtt_port" name="mqtt_port" type="number" min="1" max="65535"></div>
        <div class="row"><label for="mqtt_user">Benutzer</label>
          <input class="input" id="mqtt_user" name="mqtt_user" autocomplete="off"></div>
        <div class="row"><label for="mqtt_pass">Passwort</label>
          <input class="input" id="mqtt_pass" name="mqtt_pass" type="password" autocomplete="new-password"></div>
        <p class="hint" id="pass_hint"></p>
        <div class="row"><label for="mqtt_base">Basis-Topic</label>
          <input class="input" id="mqtt_base" name="mqtt_base"></div>
        <div class="btns"><button class="btn primary" type="submit">Speichern</button>
          <a class="btn" href="/update">Firmware-Update</a></div>
        <div class="msg" id="msg"></div>
      </section>
    </form>

    <section class="sec">
      <h2>Werkseinstellung</h2>
      <p class="hint">Löscht die Konfiguration des Empfängers: alle eingetragenen Sender mit Namen, Abständen,
        Kalibrierung und Akku-Grenzen, MQTT-Zugang und Display-Einstellung. Die Behälter werden auch aus Home Assistant entfernt.
        Danach startet der Empfänger neu. Die Sender behalten ihre eigenen Einstellungen und müssen neu gekoppelt werden.
        Am Gerät: BOOT-Taster 10 s gedrückt halten (löscht auch die WLAN-Daten).</p>
      <div class="btns">
        <button class="btn" type="button" id="b_fac">Konfiguration löschen (WLAN behalten)</button>
        <button class="btn" type="button" id="b_fac_all">Alles löschen inkl. WLAN</button>
      </div>
      <div class="msg" id="fac_msg"></div>
    </section>
  </div>
  <footer class="copyright">© 2026 R.Wisser · ESP32_FillLevel_Receiver</footer>
</div>

<script>
const $ = id => document.getElementById(id);
const STATUS = ['OK', 'Sensorfehler', 'Keine gültige Messung'];
const fmtAge = s => s < 60 ? ('vor ' + s + ' s') : s < 3600 ? ('vor ' + Math.floor(s / 60) + ' min') : ('vor ' + (s / 3600).toFixed(1) + ' h');
let st = null, conf = null, sel = -1, lastPaired = '';

const cur  = () => st && st.senders.find(x => x.idx === sel);
const ccur = () => conf && conf.senders.find(x => x.idx === sel);
function msg(id, ok, text) { $(id).textContent = text; $(id).className = 'msg ' + (ok ? 'ok' : 'bad'); }
async function post(url, params) {
  const r = await fetch(url, { method: 'POST', body: new URLSearchParams(params) });
  try { return await r.json(); } catch (e) { return { ok: false, error: 'HTTP ' + r.status }; }
}

function renderTabs() {
  const tabs = $('tabs');
  tabs.textContent = '';
  const list = st ? st.senders : [];
  $('none').classList.toggle('hide', list.length > 0);
  $('tankview').classList.toggle('hide', list.length === 0);
  for (const s of list) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'btn' + (s.idx === sel ? ' primary' : '');
    const t = document.createElement('span'); t.textContent = s.name;
    const v = document.createElement('small');
    v.textContent = !s.active ? 'ausgeschaltet'
      : (!s.valid ? 'keine Daten' : (s.level_ok ? Math.round(s.level_pct) + ' %' : 'Fehler') + (s.online ? '' : ' · offline'));
    if (!s.active) b.style.opacity = '.6';
    b.append(t, v);
    b.addEventListener('click', () => { sel = s.idx; fillTankForm(true); render(); });
    tabs.append(b);
  }
}

function renderList() {
  const l = $('slist');
  l.textContent = '';
  for (const s of st.senders) {
    const a = document.createElement('span');
    a.textContent = s.name + ' – ' + s.mac + (!s.active ? ' (aus)' : (s.online ? '' : ' (offline)'));
    const c = document.createElement('label');
    c.className = 'chk'; c.style.margin = '0';
    const cb = document.createElement('input');
    cb.type = 'checkbox'; cb.checked = s.active; cb.title = 'Behälter ein/aus (aus = wie nicht vorhanden)';
    cb.addEventListener('change', async () => {
      const j = await post('/api/fill/active', { idx: s.idx, active: cb.checked ? '1' : '0' });
      if (!j.ok) { cb.checked = !cb.checked; msg('add_msg', false, j.error || 'Fehler'); }
      await loadCfg(); poll();
    });
    c.append(cb, document.createTextNode('Aktiv'));
    const b = document.createElement('button');
    b.type = 'button'; b.className = 'btn'; b.textContent = 'Öffnen';
    b.addEventListener('click', () => { sel = s.idx; fillTankForm(true); render(); window.scrollTo(0, 0); });
    l.append(a, c, b);
  }
}

// Testmodus des Senders: wird beim nächsten Datenpaket an den Sender übertragen und von ihm bestätigt
function renderTest(s) {
  const pend = s.test_req !== null && s.test_req !== undefined;
  $('test_mode').checked = pend ? s.test_req : s.test_mode;
  $('test_mode').disabled = !s.active;
  $('test_txt').textContent = !s.active ? 'Behälter ausgeschaltet'
    : pend ? ((s.test_req ? 'EIN' : 'AUS') + ' angefordert – wird mit dem nächsten Paket des Senders übertragen')
    : !s.valid ? 'unbekannt (noch keine Daten)'
    : s.test_mode ? 'ein – kein Deep Sleep, Messung alle paar Sekunden (Akku!)' : 'aus (Messbetrieb mit Deep Sleep)';
  $('test_txt').className = pend ? '' : (s.valid && s.test_mode ? 'bad' : '');
  const opend = s.ota_req !== null && s.ota_req !== undefined;
  $('ota_mode').checked = opend ? s.ota_req : !!s.ota_mode;
  $('ota_mode').disabled = !s.active;
  $('ota_txt').textContent = !s.active ? 'Behälter ausgeschaltet'
    : opend ? ((s.ota_req ? 'EIN' : 'AUS') + ' angefordert – wird mit dem nächsten Paket des Senders übertragen')
    : !s.valid ? 'unbekannt (noch keine Daten)'
    : s.ota_mode ? 'ein – kein Deep Sleep, Sender im Heim-WLAN (Update über /update, Akku!)' : 'aus';
  $('ota_txt').className = opend ? '' : (s.valid && s.ota_mode ? 'bad' : '');
}

function renderTank() {
  const s = cur();
  if (!s) return;
  renderTest(s);
  if (!s.active) {
    $('onl').textContent = 'ausgeschaltet – Messwerte werden verworfen, kein MQTT'; $('onl').className = '';
    $('smac').textContent = s.mac;
    for (const k of ['lvl', 'dist', 'bat', 'rssi', 'age', 'pk']) $(k).textContent = '–';
    $('pct').textContent = 'AUS'; $('vol').textContent = 'Behälter ausgeschaltet';
    $('fill').style.height = '0';
    $('cal_info').textContent = 'Behälter ausgeschaltet – kein Messwert';
    return;
  }
  $('onl').textContent = !s.valid ? 'warte auf Daten' : (s.online ? 'online' : 'offline!');
  $('onl').className = s.valid && s.online ? 'ok' : (s.valid ? 'bad' : '');
  $('smac').textContent = s.mac;
  if (!s.valid) {
    for (const k of ['lvl', 'dist', 'bat', 'rssi', 'age', 'pk']) $(k).textContent = '–';
    $('pct').textContent = '--'; $('vol').textContent = 'Warte auf Daten vom Sender…';
    $('fill').style.height = '0';
    $('cal_info').textContent = 'Noch kein Messwert empfangen – bitte manuell eingeben';
    return;
  }
  if (s.level_ok) {
    $('pct').textContent = Math.round(s.level_pct) + ' %';
    $('vol').textContent = s.volume_l.toFixed(0) + ' L';
    $('lvl').textContent = s.level_cm.toFixed(1) + ' cm';
    $('fill').style.height = Math.max(0, Math.min(100, s.level_pct)) + '%';
  } else {
    $('pct').textContent = 'ERR';
    $('vol').textContent = STATUS[s.status] || 'Fehler';
    $('lvl').textContent = '–';
    $('fill').style.height = '0';
  }
  $('dist').textContent = s.distance_mm + ' mm (±' + s.spread_mm + ')';
  $('bat').textContent = s.battery_v.toFixed(2) + ' V (' + s.battery_pct + ' %)';
  $('rssi').textContent = s.rssi + ' dBm';
  $('age').textContent = fmtAge(s.age_s) + ' · Intervall ' + fmtIv(s.interval_s)
    + (s.interval_req ? ' (→ ' + fmtIv(s.interval_req) + ' angefordert)' : '');
  $('pk').textContent = s.received + ' empfangen, ' + s.lost + ' verloren';
  $('cal_info').textContent = s.status === 0
    ? ('Letzter Messwert: ' + s.distance_mm + ' mm (±' + s.spread_mm + ' mm, ' + fmtAge(s.age_s) + ')')
    : 'Letzter Messwert ungültig – bitte manuell eingeben';
}

function render() {
  if (!st) return;
  if (!st.senders.some(x => x.idx === sel)) { sel = st.senders.length ? st.senders[0].idx : -1; fillTankForm(true); }
  const n = st.senders.length;
  $('sub').textContent = n === 0 ? 'Kein Sender eingetragen'
    : n + ' Behälter · ' + st.senders.filter(x => x.active && x.online).length + ' online'
      + (st.senders.some(x => !x.active) ? ' · ' + st.senders.filter(x => !x.active).length + ' aus' : '');
  $('own').textContent = st.own_mac;
  $('ch').textContent = st.channel;
  $('cnt').textContent = n + ' von ' + st.max;
  $('mq').textContent = st.mqtt ? 'verbunden' : 'nicht verbunden';
  $('fw').textContent = (st.fw ? 'v' + st.fw : '–')
    + (st.ota_pending ? ' – neu, wird nach Selbsttest bestätigt' : '')
    + (st.ota_info ? ' – ' + st.ota_info : '');
  $('fw').className = st.ota_info ? 'bad' : '';
  $('b_pair').disabled = n >= st.max;

  if (st.pairing) msg('pair_msg', true, 'Kopplungsmodus aktiv – noch ' + st.pair_left_s + ' s. Jetzt am Sender „Empfänger suchen“ drücken.');
  else if (st.pair_full) msg('pair_msg', false, 'Kopplung abgelehnt: alle ' + st.max + ' Plätze belegt.');
  else if (st.paired) msg('pair_msg', true, 'Sender gekoppelt: ' + st.paired);
  else if ($('pair_msg').textContent.startsWith('Kopplungsmodus')) { $('pair_msg').textContent = 'Kopplungsmodus beendet.'; $('pair_msg').className = 'msg'; }
  if (st.paired && st.paired !== lastPaired) {          // neu gekoppelt -> auswählen
    lastPaired = st.paired;
    const s = st.senders.find(x => x.mac === st.paired);
    if (s) sel = s.idx;
    loadCfg().then(() => fillTankForm(true));
  }

  const u = st.unknown;
  $('unk').classList.toggle('hide', !u);
  if (u) $('unk_txt').textContent = 'Unbekannter Sender ' + u.mac + ' sendet Daten (' + fmtAge(u.age_s) + ', ' + u.distance_mm + ' mm), ist aber nicht eingetragen.';

  renderTabs();
  renderList();
  renderTank();
}

async function poll() {
  try {
    st = await (await fetch('/api/fill/state')).json();
    if (conf && conf.senders.length !== st.senders.length) await loadCfg();
    render();
  } catch (e) { $('sub').textContent = 'Verbindung unterbrochen'; }
}

function fillTankForm(clear) {
  const c = ccur();
  if (clear) { $('tmsg').textContent = ''; $('smsg').textContent = ''; $('cal_msg').textContent = ''; $('cal_mm').value = ''; }
  if (!c) return;
  for (const k of ['name', 'shape', 'dist_full', 'dist_empty', 'capacity_l']) $(k).value = c[k];
  $('active').checked = c.active;
  $('bat_empty_v').value = c.bat_empty_mv !== undefined ? (c.bat_empty_mv / 1000).toFixed(2) : '';
  $('bat_full_v').value = c.bat_full_mv !== undefined ? (c.bat_full_mv / 1000).toFixed(2) : '';
  for (const k of ['g_h', 'g_b', 'g_t', 'g_d']) $(k).value = '';
  $('g_info').textContent = 'Inhalt direkt eingeben oder aus Maßen berechnen lassen.';
}

// Inhalt aus Maßen (cm³ → L). Leere Höhe = Abstand LEER − VOLL, denn capacity_l ist der Inhalt zwischen den Marken.
function calcCap() {
  const v = id => parseFloat($(id).value) || 0;
  const marks = (v('dist_empty') - v('dist_full')) / 10;
  const lying = $('shape').value === '1';
  const h = v('g_h') || marks, b = v('g_b'), t = v('g_t');
  const d = v('g_d') || (lying ? marks : 0);       // liegend: Ø = Höhe zwischen den Marken
  let l = 0, how = '';
  if (lying) { if (d > 0 && t > 0) { l = Math.PI * d * d / 4 * t; how = 'π·(' + d + '/2)²·' + t; } }
  else if (h > 0 && d > 0) { l = Math.PI * d * d / 4 * h; how = 'π·(' + d + '/2)²·' + h; }
  else if (h > 0 && b > 0 && t > 0) { l = b * t * h; how = b + '·' + t + '·' + h; }
  if (!how) {
    $('g_info').textContent = lying ? 'Liegend: Länge (Tiefe) eingeben, Ø leer = Abstand LEER − VOLL.'
                                    : 'Stehend: Ø oder Breite + Tiefe eingeben, Höhe leer = Abstand LEER − VOLL.';
    return;
  }
  l /= 1000;
  $('capacity_l').value = l.toFixed(1);
  $('g_info').textContent = 'Inhalt = ' + how + ' cm³ = ' + l.toFixed(1) + ' L (übernommen, noch speichern).';
}
for (const k of ['g_h', 'g_b', 'g_t', 'g_d']) $(k).addEventListener('input', calcCap);
for (const k of ['shape', 'dist_full', 'dist_empty'])
  $(k).addEventListener('input', () => { if (['g_h', 'g_b', 'g_t', 'g_d'].some(g => $(g).value)) calcCap(); });

function fmtIv(s) {
  if (!s) return '–';
  if (s < 60) return s + ' s';
  const m = s / 60;
  return (Number.isInteger(m) ? m : m.toFixed(1).replace('.', ',')) + ' min';
}

async function loadCfg() {
  conf = await (await fetch('/api/fill/config')).json();
  for (const k of ['mqtt_host', 'mqtt_port', 'mqtt_user', 'mqtt_base']) $(k).value = conf[k];
  $('oled_flip').checked = conf.oled_flip;
  $('mqtt_on').checked = conf.mqtt_on;
  $('pass_hint').textContent = conf.mqtt_has_pass ? 'Passwort gespeichert – leer lassen, um es zu behalten.' : '';
  fillTankForm();
}

$('b_pair').addEventListener('click', async () => {
  const j = await post('/api/fill/pair', { action: 'start' });
  if (!j.ok) msg('pair_msg', false, j.error || 'Fehler');
  poll();
});

async function addMac(mac) {
  const j = await post('/api/fill/pair', { action: 'add', mac: mac });
  msg('add_msg', j.ok, j.ok ? 'Sender eingetragen.' : (j.error || 'Fehler'));
  if (j.ok) { sel = j.idx; $('add_mac').value = ''; await loadCfg(); fillTankForm(true); poll(); }
}
$('b_add').addEventListener('click', () => addMac($('add_mac').value.trim()));
$('b_unk').addEventListener('click', () => { if (st && st.unknown) addMac(st.unknown.mac); });

$('b_remove').addEventListener('click', async () => {
  const s = cur();
  if (!s || !confirm('Sender „' + s.name + '“ (' + s.mac + ') entfernen? Geometrie und Home-Assistant-Gerät werden gelöscht.')) return;
  const j = await post('/api/fill/pair', { action: 'remove', idx: s.idx });
  msg('smsg', j.ok, j.ok ? 'Sender entfernt.' : (j.error || 'Fehler'));
  await loadCfg(); poll();
});

$('test_mode').addEventListener('change', async () => {
  const s = cur();
  if (!s) return;
  const j = await post('/api/fill/testmode', { idx: s.idx, enabled: $('test_mode').checked ? '1' : '0' });
  if (!j.ok) msg('smsg', false, j.error || 'Fehler');
  poll();
});

$('ota_mode').addEventListener('change', async () => {
  const s = cur();
  if (!s) return;
  const j = await post('/api/fill/otamode', { idx: s.idx, enabled: $('ota_mode').checked ? '1' : '0' });
  if (!j.ok) msg('smsg', false, j.error || 'Fehler');
  poll();
});

$('b_full').addEventListener('click', () => { const s = cur(); if (s && s.valid) $('dist_full').value = s.distance_mm; });
$('b_empty').addEventListener('click', () => { const s = cur(); if (s && s.valid) $('dist_empty').value = s.distance_mm; });
$('b_bemp').addEventListener('click', () => { const s = cur(); if (s && s.valid) $('bat_empty_v').value = s.battery_v.toFixed(2); });
$('b_bful').addEventListener('click', () => { const s = cur(); if (s && s.valid) $('bat_full_v').value = s.battery_v.toFixed(2); });
$('b_cal_last').addEventListener('click', () => { const s = cur(); if (s && s.valid && s.status === 0) $('cal_mm').value = s.distance_mm; });

async function calibrate(target) {
  const p = { idx: sel, target: target };
  const v = $('cal_mm').value.trim();
  if (v !== '') p.distance_mm = v;
  const j = await post('/api/fill/calibrate', p);
  msg('cal_msg', j.ok, j.ok
    ? ((target === 'full' ? 'VOLL' : 'LEER') + ' gespeichert: voll ' + j.dist_full + ' mm / leer ' + j.dist_empty + ' mm')
    : (j.error || 'Fehler'));
  if (j.ok) { $('cal_mm').value = ''; await loadCfg(); poll(); }
}
$('b_cal_full').addEventListener('click', () => calibrate('full'));
$('b_cal_empty').addEventListener('click', () => calibrate('empty'));

$('ft').addEventListener('submit', async ev => {
  ev.preventDefault();
  const p = new URLSearchParams(new FormData($('ft')));
  p.append('idx', sel);
  const be = parseFloat($('bat_empty_v').value.replace(',', '.')), bf = parseFloat($('bat_full_v').value.replace(',', '.'));
  if (!isNaN(be) && !isNaN(bf)) {
    if (be < 2 || bf > 6 || bf < be + 0.1) { msg('tmsg', false, 'Akkuspannung: 100 % muss mindestens 0,1 V über 0 % liegen (Bereich 2,0 … 6,0 V)'); return; }
    p.append('bat_empty_mv', Math.round(be * 1000));
    p.append('bat_full_mv', Math.round(bf * 1000));
  }
  const j = await post('/api/fill/tank', p);
  if (j.ok) { await loadCfg(); poll(); }
  msg('tmsg', j.ok, j.ok ? 'Gespeichert.' : (j.error || 'Fehler'));
});

async function factory(wifi) {
  if (!confirm(wifi
    ? 'Wirklich ALLES löschen (Sender, Behälter, MQTT und WLAN-Zugang)?\nDanach startet der Empfänger im Einrichtungs-WLAN „Fuellstand-Anzeige“ (192.168.4.1).'
    : 'Konfiguration wirklich löschen (Sender, Behälter, MQTT)?\nDer WLAN-Zugang bleibt erhalten.')) return;
  const j = await post('/api/fill/factory', { confirm: '1', wifi: wifi ? '1' : '0' });
  msg('fac_msg', j.ok, j.ok ? (wifi ? 'Gelöscht – Neustart im Einrichtungs-WLAN.' : 'Gelöscht – Neustart, Seite lädt gleich neu …') : (j.error || 'Fehler'));
  if (j.ok && !wifi) setTimeout(() => location.reload(), 8000);
}
$('b_fac').addEventListener('click', () => factory(false));
$('b_fac_all').addEventListener('click', () => factory(true));

$('f').addEventListener('submit', async ev => {
  ev.preventDefault();
  const j = await post('/api/fill/config', new URLSearchParams(new FormData($('f'))));
  msg('msg', j.ok, j.ok ? 'Gespeichert.' : (j.error || 'Fehler'));
  $('mqtt_pass').value = '';
  if (j.ok) { loadCfg(); poll(); }
});

loadCfg().then(poll); setInterval(poll, 3000);
</script>
</body>
</html>
)HTML";
