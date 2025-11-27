#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <DHT.h>

const char* AP_SSID = "FIRE-FIGHTER ROBOT";
const char* AP_PASS = "NMAMIT2026"; // NOTE: ESP32 requires >=8 chars for WPA2. Code will fall back to open AP if too short.

const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

// Sensor pins
#define DHTPIN 14
#define DHTTYPE DHT11
#define MQ4_PIN 34
#define FLAME_PIN 35
#define BUZZER_PIN 25

// Motor pins (adjust if needed)
const int LEFT_MOTOR_FWD  = 32;
const int LEFT_MOTOR_REV  = 33;
const int RIGHT_MOTOR_FWD = 26;
const int RIGHT_MOTOR_REV = 27;

// History buffer
const int HISTORY_SIZE = 120;
struct Sample {
  unsigned long t;   // relative millis timestamp
  float temp;
  float hum;
  int mq_adc;
  float mq_percent;
  bool flame;
};
Sample history[HISTORY_SIZE];
int histIndex = 0;
int histCount = 0;

DHT dht(DHTPIN, DHTTYPE);

unsigned long lastSampleMillis = 0;
const unsigned long SAMPLE_INTERVAL = 1000;
unsigned long lastBuzzerMillis = 0;
const unsigned long BUZZER_MIN_INTERVAL = 1000;

const bool ACTIVE_LOW_FLAME = false;  // set true if flame sensor outputs LOW on flame detection
const unsigned long MOVE_TIMEOUT = 500; // ms auto motor pulse length

bool moving = false;
unsigned long moveStartMillis = 0;

void pushSample(float t, float h, int mqraw, bool flame) {
  Sample &s = history[histIndex];
  s.t = millis();
  s.temp = t;
  s.hum = h;
  s.mq_adc = mqraw;
  s.mq_percent = mqraw * 100.0 / 4095.0;
  s.flame = flame;
  histIndex = (histIndex + 1) % HISTORY_SIZE;
  if (histCount < HISTORY_SIZE) histCount++;
}

// Motor control helpers
void stopMotors() {
  digitalWrite(LEFT_MOTOR_FWD, LOW);
  digitalWrite(LEFT_MOTOR_REV, LOW);
  digitalWrite(RIGHT_MOTOR_FWD, LOW);
  digitalWrite(RIGHT_MOTOR_REV, LOW);
  moving = false;
}

void moveForward() {
  digitalWrite(LEFT_MOTOR_FWD, HIGH);
  digitalWrite(LEFT_MOTOR_REV, LOW);
  digitalWrite(RIGHT_MOTOR_FWD, HIGH);
  digitalWrite(RIGHT_MOTOR_REV, LOW);
  moving = true;
  moveStartMillis = millis();
}

void moveBackward() {
  digitalWrite(LEFT_MOTOR_FWD, LOW);
  digitalWrite(LEFT_MOTOR_REV, HIGH);
  digitalWrite(RIGHT_MOTOR_FWD, LOW);
  digitalWrite(RIGHT_MOTOR_REV, HIGH);
  moving = true;
  moveStartMillis = millis();
}

void turnLeft() {
  digitalWrite(LEFT_MOTOR_FWD, LOW);
  digitalWrite(LEFT_MOTOR_REV, HIGH);
  digitalWrite(RIGHT_MOTOR_FWD, HIGH);
  digitalWrite(RIGHT_MOTOR_REV, LOW);
  moving = true;
  moveStartMillis = millis();
}

void turnRight() {
  digitalWrite(LEFT_MOTOR_FWD, HIGH);
  digitalWrite(LEFT_MOTOR_REV, LOW);
  digitalWrite(RIGHT_MOTOR_FWD, LOW);
  digitalWrite(RIGHT_MOTOR_REV, HIGH);
  moving = true;
  moveStartMillis = millis();
}

// Web pages go here, each return String with HTML + CSS + JS as needed
String pageRoot() {
  return R"rawliteral(
<!doctype html>
<html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fire-Fighter Robo Conntroller</title>
<style>
  :root{--accent:#2563eb;--muted:#9aa4b2;}
  html,body{height:100%;margin:0}
  body { font-family: system-ui, -apple-system, "Segoe UI", Roboto, Helvetica, Arial; background: linear-gradient(180deg,#071024,#0a1a2b); color: #e6eef6; padding: 14px; box-sizing: border-box; }
  .wrap { max-width: 980px; margin: 0 auto; }
  .card { background: rgba(255,255,255,0.02); padding: 16px; border-radius: 12px; }
  .header { display:flex; justify-content:space-between; align-items:center; gap:12px; flex-wrap:wrap;}
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(140px,1fr)); gap:12px; margin-top:12px; }
  .stat { background: rgba(255,255,255,0.01); padding: 10px; border-radius: 8px; }
  .label { font-size: 12px; color: var(--muted); }
  .value { font-size: 20px; font-weight: 700; margin-top: 4px; }
  #flame.value { color: #5eead4; }
  .controls { margin-top: 14px;}
  .ctrl-btn {
    background: var(--accent);
    color: white;
    border:none;
    padding: 12px 24px;
    border-radius: 12px;
    font-weight: 700;
    cursor: pointer;
    font-size: 16px;
    transition: background-color 0.3s ease, transform 0.2s ease;
  }
  .ctrl-btn:hover {
    background-color: #1d4ed8;
    transform: scale(1.05);
  }
  pre { background: rgba(0,0,0,0.16); padding: 10px; border-radius: 8px; color: #cfe6ff; margin-top: 12px; overflow-x:auto; max-height: 240px; }
  /* Intro overlay */
  #introOverlay {
    position: fixed; left: 0; top:0; width: 100%; height:100%; z-index: 9999;
    display: flex; align-items: center; justify-content: center; background: linear-gradient(180deg,rgba(2,6,23,0.95),rgba(2,6,23,0.98));
    color:#fff; flex-direction: column; padding: 20px; box-sizing: border-box;
  }
  .loader-box { background: rgba(255,255,255,0.02); padding: 22px; border-radius: 12px; text-align: center; max-width: 720px; }
  .title { font-size: 28px; font-weight: 800; margin-bottom: 6px; }
  .dev { font-size: 14px; color: #9aa4b2; margin-top: 6px; }
  .spinner {
    width: 56px; height: 56px; border-radius: 50%; border: 6px solid rgba(255,255,255,0.08);
    border-top-color: #ff6b6b; animation: spin 1s linear infinite; margin: 12px auto;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
  .fadeOut {
    animation: fadeOutAnim 0.6s ease both;
  }
  @keyframes fadeOutAnim { to { opacity:0; visibility:hidden; transform: scale(0.98); } }
</style>
</head>
<body>

<!-- Intro overlay -->
<div id="introOverlay">
  <div class="loader-box">
    <div class="title">Fire fighter Robot</div>
    <div style="font-weight:700">DEVELOPED BY</div>
    <div class="dev">ROHAN T KINI - NNM22CS187</div>
    <div class="dev">SPOORTHI S KOTIAN - NNM22CS178</div>
    <div class="spinner"></div>
    <div style="color:#9aa4b2;margin-top:8px">Loading portal — please wait</div>
  </div>
</div>

<div class="wrap" id="mainContent" style="visibility:hidden">
  <div class="card">
    <div class="header">
      <div>
        <h2 style="margin:0">Welcome to FF Portal</h2>
        <div style="color:var(--muted);margin-top:6px">SoftAP: <b>)rawliteral" + String(AP_SSID) + R"rawliteral(</b></div>
      </div>
      <div style="text-align:left">
        <div style="color:var(--muted)">Controller    •    Details</div>
        <div style="margin-top:8px">
          <button id="btnControls" class="ctrl-btn">Controls</button>
          <button id="btnDetails" class="ctrl-btn" style="margin-left:8px">See Details</button>
        </div>
      </div>
    </div>

    <div class="grid" style="margin-top:14px">
      <div class="stat"><div class="label">Temperature</div><div id="temp" class="value">-- °C</div></div>
      <div class="stat"><div class="label">Humidity</div><div id="hum" class="value">-- %</div></div>
      <div class="stat"><div class="label">CNG (MQ-4 %)</div><div id="mq" class="value">-- %</div></div>
      <div class="stat"><div class="label">Flame</div><div id="flame" class="value" style="color:#5eead4">No Flame</div></div>
    </div>

    <div class="controls" style="margin-top:14px">
      <div style="flex:1"></div>
      <div class="small">Use the <b>Controls</b> button to open the gamepad-style control page.</div>
    </div>

    <pre id="raw">Waiting for data...</pre>
  </div>
</div>

<script>
// Show intro then fade and show content
window.addEventListener('load', ()=>{
  const overlay = document.getElementById('introOverlay');
  const main = document.getElementById('mainContent');
  setTimeout(()=>{
    overlay.classList.add('fadeOut');
    setTimeout(()=>{
      overlay.style.display = 'none';
      main.style.visibility = 'visible';
    }, 620);
  }, 5000);
});

document.addEventListener('DOMContentLoaded', ()=>{
  document.getElementById('btnControls').onclick = ()=> location.href = '/controls';
  document.getElementById('btnDetails').onclick = ()=> location.href = '/details';
});

async function updateSummary(){
  try {
    const r = await fetch('/sensor');
    const j = await r.json();
    if (!j.ok) 
      return;
    document.getElementById('temp').innerText = j.temperature.toFixed(1) + ' °C';
    document.getElementById('hum').innerText = j.humidity.toFixed(1) + ' %';
    document.getElementById('mq').innerText = j.mq_percent.toFixed(1) + ' %';
    document.getElementById('flame').innerText = j.flame ? 'Flame Detected!' : 'No Flame';
    document.getElementById('flame').style.color = j.flame ? '#ff6b6b' : '#5eead4';
    document.getElementById('raw').innerText = JSON.stringify(j,null,2);
  } catch(e) {
    document.getElementById('raw').innerText = 'Error: '+ e;
  }
}
setInterval(updateSummary, 1000);
updateSummary();
</script>

</body></html>
)rawliteral";
}

// ---- UPDATED Controls page: improved alignment, spacing, responsive ----
String pageControls() {
  return R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Controls</title>
<style>
  :root{--bg:#071526;--card:#0b2230;--accent:#2563eb;--danger:#ff3b30}
  html,body{height:100%;margin:0}
  body{font-family:system-ui, -apple-system, "Segoe UI", Roboto, Helvetica, Arial; background:var(--bg); color:#e6eef6; display:flex; align-items:center; justify-content:center; padding:16px; box-sizing:border-box;}
  .card{background:linear-gradient(180deg, rgba(255,255,255,0.01), rgba(255,255,255,0.005)); padding:22px; border-radius:14px; width:100%; max-width:520px; box-shadow:0 12px 30px rgba(0,0,0,0.45);}
  .top{display:flex;justify-content:space-between;align-items:center}
  h3{margin:0;font-size:20px}
  .grid-wrap{display:flex;flex-direction:column;align-items:center;gap:18px;margin-top:18px}
  .row{display:flex;gap:18px;align-items:center;justify-content:center;width:100%}
  .big-btn{background:var(--accent);color:white;border:none;padding:18px 24px;border-radius:12px;font-weight:800;font-size:20px;cursor:pointer;min-width:120px;box-shadow:0 8px 18px rgba(37,99,235,0.18)}
  .big-btn:active{transform:translateY(2px)}
  .fire{background:var(--danger);box-shadow:0 8px 18px rgba(255,59,48,0.18)}
  .hint{color:#9aa4b2;font-size:13px;text-align:center;margin-top:12px}
  /* Responsive: on small screens stack into a single column */
  @media (max-width:460px){
    .row{flex-direction:row;gap:12px}
    .big-btn{min-width:72px;padding:14px 14px;font-size:18px}
    .grid-wrap{gap:12px}
  }
  /* blue small back button style */
  .small-back { background: var(--accent); color: #fff; border:none; padding:8px 12px; border-radius:10px; font-weight:700; cursor:pointer; box-shadow:0 6px 14px rgba(37,99,235,0.15); }
</style>
</head>
<body>
  <div class="card" role="main" aria-label="Controls">
    <div class="top">
      <div><h3>Controls</h3><div style="color:#9aa4b2;font-size:13px">Gamepad-style controls</div></div>
      <div><button class="small-back" onclick="location.href='/'">Back</button></div>
    </div>

    <div class="grid-wrap">
      <!-- Forward centered -->
      <div class="row" style="margin-top:6px;">
        <button id="btnForward" class="big-btn" aria-label="Forward">↑</button>
      </div>

      <!-- Left | Fire | Right -->
      <div class="row" style="margin-top:6px;">
        <button id="btnLeft" class="big-btn" aria-label="Left">←</button>
        <button id="btnFire" class="big-btn fire" aria-label="Fire">FIRE</button>
        <button id="btnRight" class="big-btn" aria-label="Right">→</button>
      </div>

      <!-- Back centered -->
      <div class="row" style="margin-bottom:6px;">
        <button id="btnBack" class="big-btn"  aria-label="Back">↓</button>
      </div>

      <div class="hint">Use keyboard arrows for movement. Press Space to fire.</div>
    </div>
  </div>

<script>
  async function sendControl(cmd){
    try {
      const res = await fetch('/control?cmd=' + encodeURIComponent(cmd));
      const j = await res.json();
      console.log('control', j);
    } catch(e) {
      console.warn('control error', e);
    }
  }

  document.getElementById('btnForward').onclick = () => sendControl('forward');
  document.getElementById('btnBack').onclick = () => sendControl('back');
  document.getElementById('btnLeft').onclick = () => sendControl('left');
  document.getElementById('btnRight').onclick = () => sendControl('right');
  document.getElementById('btnFire').onclick = () => sendControl('fire');

  // keyboard support
  window.addEventListener('keydown', (e)=>{
    if(e.repeat) return;
    if(e.key === 'ArrowUp') sendControl('forward');
    else if(e.key === 'ArrowDown') sendControl('back');
    else if(e.key === 'ArrowLeft') sendControl('left');
    else if(e.key === 'ArrowRight') sendControl('right');
    else if(e.code === 'Space') { e.preventDefault(); sendControl('fire'); }
  });
</script>

</body>
</html>
)rawliteral";
}

String pageDetails() {
  return R"rawliteral(
<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Details</title>
<style>
body{font-family:system-ui;background:#071024;color:#e6eef6;margin:0;padding:12px}
.card{max-width:1100px;margin:0 auto;background:rgba(255,255,255,0.01);padding:12px;border-radius:10px}
.btn{padding:8px 12px;border-radius:8px;border:none;background:#2563eb;color:white;cursor:pointer}
table{width:100%;border-collapse:collapse;margin-top:12px}
th,td{padding:8px;border-bottom:1px solid rgba(255,255,255,0.04);text-align:left}
th{color:#9aa4b2;font-size:13px}
.value-up{color:#16a34a;font-weight:700;margin-left:6px}
.value-down{color:#ef4444;font-weight:700;margin-left:6px}
.small{color:#9aa4b2;margin-top:8px}
</style>
</head>
<body>
<div class="card">
  <div style="display:flex;justify-content:space-between;align-items:center">
    <div><h3 style="margin:0">Sensor Details</h3><div class="small">Live table — ▲ increase, ▼ decrease</div></div>
    <div><button onclick="location.href='/'" class="btn">Back</button></div>
  </div>

  <table id="histTable"><thead><tr><th>Time</th><th>Temp (°C)</th><th>Hum (%)</th><th>CNG %</th><th>MQ ADC</th><th>Flame</th></tr></thead><tbody></tbody></table>
</div>

<script>
function compareArrow(prev, curr){
  if(prev===null || prev===undefined) return '';
  if(curr > prev) return '<span class="value-up">▲</span>';
  if(curr < prev) return '<span class="value-down">▼</span>';
  return '';
}

function renderTable(hist){
  const tbody = document.querySelector('#histTable tbody');
  tbody.innerHTML = '';
  for(let i=0;i<hist.length;i++){
    const row = hist[i];
    const prev = (i>0) ? hist[i-1] : null;
    const tr = document.createElement('tr');
    const time = new Date(Date.now() - (hist.length - 1 - i)*1000).toLocaleTimeString();
    tr.innerHTML = `
      <td>${time}</td>
      <td>${row.temp.toFixed(2)} ${compareArrow(prev?prev.temp:null,row.temp)}</td>
      <td>${row.hum.toFixed(2)} ${compareArrow(prev?prev.hum:null,row.hum)}</td>
      <td>${row.mq_percent.toFixed(2)} ${compareArrow(prev?prev.mq_percent:null,row.mq_percent)}</td>
      <td>${row.mq_adc}</td>
      <td>${row.flame ? '1' : '0'}</td>
    `;
    tbody.appendChild(tr);
  }
}

async function updateDetails(){
  try{
    const r = await fetch('/sensor');
    const j = await r.json();
    if(!j.ok) return;
    renderTable(j.history || []);
  }catch(e){
    console.warn(e);
  }
}
updateDetails();
setInterval(updateDetails, 1000);
</script>

</body></html>
)rawliteral";
}

// Handlers
void handleRoot() { server.send(200, "text/html", pageRoot()); }
void handleControls() { server.send(200, "text/html", pageControls()); }
void handleDetails() { server.send(200, "text/html", pageDetails()); }

// Control commands handler
void handleControl() {
  if (!server.hasArg("cmd")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_cmd\"}");
    return;
  }
  String cmd = server.arg("cmd");
  cmd.toLowerCase();

  Serial.printf("Control command received: %s\n", cmd.c_str());

  if (cmd == "forward") moveForward();
  else if (cmd == "back") moveBackward();
  else if (cmd == "left") turnLeft();
  else if (cmd == "right") turnRight();
  else if (cmd == "stop") stopMotors();
  else if (cmd == "fire") {
    if (millis() - lastBuzzerMillis > BUZZER_MIN_INTERVAL) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(300);
      digitalWrite(BUZZER_PIN, LOW);
      lastBuzzerMillis = millis();
    }
  } else {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_cmd\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true,\"cmd\":\"" + cmd + "\"}");
}

// Sensor data JSON including history
void handleSensor() {
  if (histCount == 0) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"no_data\"}");
    return;
  }
  int lastIdx = (histIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE;
  Sample &s = history[lastIdx];

  String j = "{\"ok\":true,";
  j += "\"timestamp\":" + String(millis()) + ",";
  j += "\"temperature\":" + String(isnan(s.temp) ? 0.0 : s.temp, 2) + ",";
  j += "\"humidity\":" + String(isnan(s.hum) ? 0.0 : s.hum, 2) + ",";
  j += "\"mq_adc\":" + String(s.mq_adc) + ",";
  j += "\"mq_percent\":" + String(s.mq_percent, 2) + ",";
  j += "\"flame\":" + String(s.flame ? "true" : "false") + ",";
  j += "\"history\":[";
  int start = (histIndex - histCount + HISTORY_SIZE) % HISTORY_SIZE;
  for (int i = 0; i < histCount; ++i) {
    int idx = (start + i) % HISTORY_SIZE;
    Sample &h = history[idx];
    j += "{\"t\":" + String(h.t) + ",";
    j += "\"temp\":" + String(isnan(h.temp) ? 0.0 : h.temp, 2) + ",";
    j += "\"hum\":" + String(isnan(h.hum) ? 0.0 : h.hum, 2) + ",";
    j += "\"mq_percent\":" + String(h.mq_percent, 2) + ",";
    j += "\"mq_adc\":" + String(h.mq_adc) + ",";
    j += "\"flame\":" + String(h.flame ? "true" : "false") + "}";
    if (i < histCount - 1) j += ",";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// NotFound redirect to main page
void handleNotFound() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Sensor pins
  pinMode(FLAME_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  dht.begin();
  analogReadResolution(12);
  analogSetPinAttenuation(MQ4_PIN, ADC_11db);

  // Motor pins init
  pinMode(LEFT_MOTOR_FWD, OUTPUT);
  pinMode(LEFT_MOTOR_REV, OUTPUT);
  pinMode(RIGHT_MOTOR_FWD, OUTPUT);
  pinMode(RIGHT_MOTOR_REV, OUTPUT);
  stopMotors();

  // WiFi AP + captive DNS
  WiFi.mode(WIFI_AP);
  // start AP with password if long enough, else start open and warn
  if (strlen(AP_PASS) >= 8) {
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("Started secured AP '%s' (WPA2)\n", AP_SSID);
  } else {
    WiFi.softAP(AP_SSID);
    Serial.printf("Started OPEN AP '%s' (password too short: using open). To secure, set AP_PASS >= 8 chars.\n", AP_SSID);
  }

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP: "); Serial.println(apIP);
  dnsServer.start(DNS_PORT, "*", apIP);

  // HTTP routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/controls", HTTP_GET, handleControls);
  server.on("/details", HTTP_GET, handleDetails);
  server.on("/sensor", HTTP_GET, handleSensor);
  server.on("/control", HTTP_GET, handleControl);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started.");

  // Init history store
  for (int i = 0; i < HISTORY_SIZE; ++i) {
    history[i].t = 0;
    history[i].temp = NAN;
    history[i].hum = NAN;
    history[i].mq_adc = 0;
    history[i].mq_percent = 0;
    history[i].flame = false;
  }
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  unsigned long now = millis();

  // Stop motors after timeout for safety
  if (moving && (now - moveStartMillis >= MOVE_TIMEOUT)) {
    stopMotors();
    Serial.println("Movement timeout — motors stopped.");
  }

  if (now - lastSampleMillis >= SAMPLE_INTERVAL) {
    lastSampleMillis = now;

    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int mqraw = analogRead(MQ4_PIN);
    int flameRaw = digitalRead(FLAME_PIN);
    bool flameNow = ACTIVE_LOW_FLAME ? (flameRaw == LOW) : (flameRaw == HIGH);

    // Automatic buzzer on flame, rate-limited
    if (flameNow) {
      Serial.println("Flame Detected!");
      if (millis() - lastBuzzerMillis > BUZZER_MIN_INTERVAL) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(150);
        digitalWrite(BUZZER_PIN, LOW);
        lastBuzzerMillis = millis();
      }
    }

    pushSample(isnan(t) ? 0 : t,
               isnan(h) ? 0 : h,
               mqraw,
               flameNow);

    Serial.printf("t:%.1fC h:%.1f%% mq:%d (%.1f%%) flame:%d\n",
                  isnan(t) ? 0 : t,
                  isnan(h) ? 0 : h,
                  mqraw,
                  mqraw * 100.0 / 4095.0,
                  flameNow ? 1 : 0);
  }
}
