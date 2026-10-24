/*
  ESP32 4-Relay Home Automation
  - SinricPro for Alexa/Google
  - Simple web UI with 4 toggles (AJAX)
  - Replace placeholders (WiFi, Sinric keys, device IDs)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <SinricPro.h>
#include <SinricProSwitch.h>

// ---------- USER CONFIG ----------
const char* WIFI_SSID = "Your router ssid name";
const char* WIFI_PASS = "password_of_your_wifi";

const char* SINRIC_APP_KEY    = "Change with your app key";   // from SinricPro dashboard
const char* SINRIC_APP_SECRET = "change with your app secret key"; // from SinricPro dashboard

// SinricPro device IDs (create 4 Switch devices in SinricPro and paste their IDs)
const char* DEVICE_ID_0 = "device id for relay 1";
const char* DEVICE_ID_1 = "device id for relay 2";
const char* DEVICE_ID_2 = "device id for relay 3";
const char* DEVICE_ID_3 = "device id for relay 4";

// Relay pin mapping (change to match your wiring)
const int relayPins[4] = {18, 19, 21, 22};

// Set to LOW if relay is active LOW (typical). Set to HIGH if active HIGH.
const int RELAY_ACTIVE_LEVEL = LOW;
// ----------------------------------

WebServer server(80);

// maintain current known states
bool relayState[4] = {false, false, false, false};

// helper to set relay hardware
void setRelayHardware(int idx, bool on) {
  if (idx < 0 || idx > 3) return;
  digitalWrite(relayPins[idx], on ? RELAY_ACTIVE_LEVEL : (RELAY_ACTIVE_LEVEL == LOW ? HIGH : LOW));
  relayState[idx] = on;
}

// Map Sinric deviceId to relay index
int deviceIdToIndex(const String &deviceId) {
  if (deviceId == DEVICE_ID_0) return 0;
  if (deviceId == DEVICE_ID_1) return 1;
  if (deviceId == DEVICE_ID_2) return 2;
  if (deviceId == DEVICE_ID_3) return 3;
  return -1;
}

// SinricPro callback (handles power state requests from Alexa/Google)
bool onPowerState(const String &deviceId, bool &state) {
  int idx = deviceIdToIndex(deviceId);
  if (idx < 0) return false;
  Serial.printf("SinricPro request: device %s -> %s\n", deviceId.c_str(), state ? "ON" : "OFF");
  setRelayHardware(idx, state);
  // send event back to Sinric server to confirm new state
  SinricProSwitch &dev = SinricPro[deviceId];
  dev.sendPowerStateEvent(state);
  return true; // handled
}

// Serve the web UI
const char indexPage[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <title>ESP32 4-Relay Control</title>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <style>
    body { font-family: Arial, Helvetica, sans-serif; padding:10px; background:#f0f0f0; }
    .card { background:#fff; padding:16px; border-radius:8px; box-shadow:0 2px 6px rgba(0,0,0,0.15); max-width:480px; margin:auto; }
    h2 { margin: 0 0 8px; }
    .row { display:flex; align-items:center; justify-content:space-between; margin:12px 0; }
    .btn { padding:10px 16px; border-radius:6px; border: none; cursor:pointer; font-weight:600; }
    .on { background:#2ecc71; color:white; }
    .off { background:#e74c3c; color:white; }
    .status { font-size:0.9em; color:#666; }
  </style>
</head>
<body>
  <div class="card">
    <h2>ESP32 — 4 Relay Control</h2>
    <div id="controls"></div>
    <p class="status">Status: <span id="wifistatus">connecting...</span></p>
  </div>

<script>
const relayCount = 4;

function createControls(states) {
  const container = document.getElementById('controls');
  container.innerHTML = '';
  for (let i=0;i<relayCount;i++){
    const row = document.createElement('div');
    row.className = 'row';
    const label = document.createElement('div');
    label.innerText = 'Relay ' + (i+1);
    const btn = document.createElement('button');
    btn.id = 'btn'+i;
    btn.className = 'btn ' + (states[i] ? 'on' : 'off');
    btn.innerText = states[i] ? 'ON' : 'OFF';
    btn.onclick = () => toggleRelay(i);
    row.appendChild(label);
    row.appendChild(btn);
    container.appendChild(row);
  }
}

async function fetchStatus() {
  try {
    const resp = await fetch('/status');
    const j = await resp.json();
    document.getElementById('wifistatus').innerText = j.ip || 'connected';
    createControls(j.states);
  } catch (e) {
    document.getElementById('wifistatus').innerText = 'disconnected';
    console.error(e);
  }
}

async function toggleRelay(idx) {
  const btn = document.getElementById('btn'+idx);
  const newState = !(btn.classList.contains('on'));
  // send request to ESP
  try {
    await fetch(`/relay?idx=${idx}&state=${newState ? 1 : 0}`);
    // optimistic UI update
    btn.classList.toggle('on', newState);
    btn.classList.toggle('off', !newState);
    btn.innerText = newState ? 'ON' : 'OFF';
  } catch(e) {
    console.error(e);
  }
}

setInterval(fetchStatus, 2000);
fetchStatus();
</script>
</body>
</html>
)rawliteral";

// endpoint: /status returns JSON with states and IP
void handleStatus() {
  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"states\":[";
  for (int i=0;i<4;i++) {
    json += (relayState[i] ? "1" : "0");
    if (i<3) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// endpoint: /relay?idx=N&state=1
void handleRelayToggle() {
  if (!server.hasArg("idx") || !server.hasArg("state")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  int idx = server.arg("idx").toInt();
  int state = server.arg("state").toInt();
  if (idx < 0 || idx > 3) { server.send(400, "text/plain", "Invalid idx"); return; }

  bool newState = (state != 0);
  setRelayHardware(idx, newState);

  // send feedback to SinricPro server
  const char* deviceId = nullptr;
  switch(idx) {
    case 0: deviceId = DEVICE_ID_0; break;
    case 1: deviceId = DEVICE_ID_1; break;
    case 2: deviceId = DEVICE_ID_2; break;
    case 3: deviceId = DEVICE_ID_3; break;
  }
  if (deviceId) {
    SinricProSwitch &mySwitch = SinricPro[deviceId];
    mySwitch.sendPowerStateEvent(newState);
  }

  server.send(200, "application/json", String("{\"ok\":1, \"idx\":") + idx + ",\"state\":" + (newState?1:0) + "}");
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](){ server.send_P(200, "text/html", indexPage); });
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/relay", HTTP_GET, handleRelayToggle);
  server.begin();
  Serial.println("[web] server started");
}

void setupRelays() {
  for (int i=0;i<4;i++){
    pinMode(relayPins[i], OUTPUT);
    // start OFF
    digitalWrite(relayPins[i], (RELAY_ACTIVE_LEVEL == LOW) ? HIGH : LOW);
    relayState[i] = false;
  }
}

void setupWiFi() {
  Serial.printf("\n[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tryc = 0;
  while (WiFi.status() != WL_CONNECTED && tryc < 40) {
    delay(500);
    Serial.print(".");
    tryc++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] connected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] failed to connect");
  }
}

void setupSinricPro() {
  // create devices and set callback
  SinricProSwitch &s0 = SinricPro[DEVICE_ID_0];
  SinricProSwitch &s1 = SinricPro[DEVICE_ID_1];
  SinricProSwitch &s2 = SinricPro[DEVICE_ID_2];
  SinricProSwitch &s3 = SinricPro[DEVICE_ID_3];

  s0.onPowerState(onPowerState);
  s1.onPowerState(onPowerState);
  s2.onPowerState(onPowerState);
  s3.onPowerState(onPowerState);

  SinricPro.onConnected([](){ Serial.println("[SinricPro] Connected"); });
  SinricPro.onDisconnected([](){ Serial.println("[SinricPro] Disconnected"); });

  SinricPro.begin(SINRIC_APP_KEY, SINRIC_APP_SECRET);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  setupRelays();
  setupWiFi();
  setupWebServer();

  // init SinricPro (Alexa & Google control)
 // SinricPro.onPowerState([](const String &deviceId, bool &state)->bool { return onPowerState(deviceId, state); });
  setupSinricPro();
}

void loop() {
  SinricPro.handle();     // handle sinric
  server.handleClient();  // handle webserver
}
