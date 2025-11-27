// Fire-Fighter Robot — Bluetooth + sensors + motors + history
// Bluetooth commands (newline-terminated): forward, back, left, right, stop, fire, get_sensor, ping

#include <BluetoothSerial.h>
#include <DHT.h>
#include <ArduinoJson.h>

// --- Pins & configs (from your request) ---
#define DHTPIN 14
#define DHTTYPE DHT11
#define MQ4_PIN 34
#define FLAME_PIN 35
#define BUZZER_PIN 25

const int LEFT_MOTOR_FWD  = 32;
const int LEFT_MOTOR_REV  = 33;
const int RIGHT_MOTOR_FWD = 26;
const int RIGHT_MOTOR_REV = 27;

const bool ACTIVE_LOW_FLAME = false;
const unsigned long MOVE_TIMEOUT = 500; // ms
const unsigned long SAMPLE_INTERVAL = 1000; // ms telemetry interval
const unsigned long BUZZER_MIN_INTERVAL = 1000; // ms
const int HISTORY_SIZE = 120;      

// --- Globals ---
BluetoothSerial SerialBT;
DHT dht(DHTPIN, DHTTYPE);

struct Sample {
  unsigned long t;
  float temp;
  float hum;
  int mq_adc;
  float mq_percent;
  bool flame;
};
Sample history[HISTORY_SIZE];
int histIndex = 0;
int histCount = 0;

unsigned long lastSampleMillis = 0;
unsigned long lastBuzzerMillis = 0;
unsigned long lastBtBroadcast = 0;    // <-- added for BT periodic summary

bool moving = false;
unsigned long moveStartMillis = 0;

// --- Helpers: motors / buzzer / history ---
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

void buzzOnce(unsigned long ms = 150) {
  if (millis() - lastBuzzerMillis > BUZZER_MIN_INTERVAL) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(ms);
    digitalWrite(BUZZER_PIN, LOW);
    lastBuzzerMillis = millis();
  }
}

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

// Read sensors once and store sample
void sampleSensors() {
  float hum = dht.readHumidity();
  float temp = dht.readTemperature();
  // If read fails, dht returns NAN; keep that handled in pushSample (we'll send 0 later if NAN)
  int mqraw = analogRead(MQ4_PIN);
  int flameRaw = digitalRead(FLAME_PIN);
  bool flameNow = ACTIVE_LOW_FLAME ? (flameRaw == LOW) : (flameRaw == HIGH);

  // automatic buzzer if flame
  if (flameNow) {
    Serial.println("Flame Detected!");
    buzzOnce(150);
  }

  pushSample(isnan(temp) ? 0.0f : temp,
             isnan(hum) ? 0.0f : hum,
             mqraw,
             flameNow);

  Serial.printf("Sample: T=%.1f H=%.1f MQ=%d (%.1f%%) Flame=%d\n",
                isnan(temp)?0.0f:temp,
                isnan(hum)?0.0f:hum,
                mqraw,
                mqraw * 100.0 / 4095.0,
                flameNow ? 1 : 0);
}

// --- Bluetooth JSON builder & sender ---
// We limit historySent to keep JSON small: change HISTORY_SEND_LIMIT if needed
const int HISTORY_SEND_LIMIT = 30; // send last 30 samples at most

void sendSensorData() {
  // Choose a StaticJsonDocument size:
  // estimated per-entry ~ 80-100 bytes; for 30 entries -> ~3000-4000 bytes
  // We'll allocate 6144 bytes; adjust if you get memory issues.
  StaticJsonDocument<6144> doc;

  if (histCount == 0) {
    doc["ok"] = false;
    doc["error"] = "no_data";
    String out;
    serializeJson(doc, out);
    SerialBT.println(out);
    return;
  }

  int lastIdx = (histIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE;
  Sample &s = history[lastIdx];

  doc["ok"] = true;
  doc["timestamp"] = millis();
  doc["temperature"] = s.temp;
  doc["humidity"] = s.hum;
  doc["mq_percent"] = s.mq_percent;
  doc["mq_adc"] = s.mq_adc;
  doc["flame"] = s.flame;

  int toSend = min(histCount, HISTORY_SEND_LIMIT);
  JsonArray hist = doc.createNestedArray("history");

  // send most recent 'toSend' items in chronological order (oldest -> newest)
  int start = (histIndex - toSend + HISTORY_SIZE) % HISTORY_SIZE;
  for (int i = 0; i < toSend; ++i) {
    int idx = (start + i) % HISTORY_SIZE;
    JsonObject obj = hist.createNestedObject();
    obj["t"] = history[idx].t;
    obj["temp"] = history[idx].temp;
    obj["hum"] = history[idx].hum;
    obj["mq_percent"] = history[idx].mq_percent;
    obj["mq_adc"] = history[idx].mq_adc;
    obj["flame"] = history[idx].flame;
  }

  String output;
  serializeJson(doc, output); // serialize to String
  SerialBT.println(output);
}

// --- New: send compact summary (no history) for periodic broadcasts ---
void sendSensorSummary() {
  if (histCount == 0) {
    SerialBT.println("{\"ok\":false,\"error\":\"no_data\"}");
    return;
  }
  int lastIdx = (histIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE;
  Sample &s = history[lastIdx];

  // Build a small JSON string (compact)
  char buf[200];
  int len = snprintf(buf, sizeof(buf),
    "{\"ok\":true,\"timestamp\":%lu,\"temperature\":%.2f,\"humidity\":%.2f,\"mq_percent\":%.2f,\"mq_adc\":%d,\"flame\":%s}",
    millis(),
    isnan(s.temp) ? 0.0f : s.temp,
    isnan(s.hum) ? 0.0f : s.hum,
    s.mq_percent,
    s.mq_adc,
    s.flame ? "true" : "false"
  );
  SerialBT.println(buf);
}

// --- Bluetooth command handler ---
void handleBluetoothCommand(const String &inRaw) {
  String line = inRaw;
  line.trim();
  if (line.length() == 0) return;
  String cmd = line;
  cmd.toLowerCase();

  Serial.printf("BT cmd: '%s'\n", cmd.c_str());

  if (cmd == "forward") { moveForward(); return; }
  if (cmd == "back")    { moveBackward(); return; }
  if (cmd == "left")    { turnLeft(); return; }
  if (cmd == "right")   { turnRight(); return; }
  if (cmd == "stop")    { stopMotors(); return; }
  if (cmd == "fire")    { buzzOnce(300); return; }
  if (cmd == "get_sensor" || cmd == "get" || cmd == "sensor" || cmd == "get_sensor\n") { sendSensorData(); return; }
  if (cmd == "ping") { SerialBT.println("{\"ok\":true,\"reply\":\"pong\"}"); return; }

  // Support simple HTTP-like GET forms too:
  // GET /control?cmd=forward
  if (cmd.startsWith("get ") || cmd.startsWith("get/") || cmd.startsWith("get/")) {
    // normalize
    int p = cmd.indexOf('/');
    String path = (p>=0) ? cmd.substring(p) : cmd;
    path.trim();
    // very simple parsing
    if (path.startsWith("/control") && path.indexOf("cmd=") >= 0) {
      int i = path.indexOf("cmd=");
      String arg = path.substring(i + 4);
      arg.trim();
      arg.toLowerCase();
      handleBluetoothCommand(arg); // recursive safe since arg is small
      return;
    } else if (path.startsWith("/sensor")) {
      sendSensorData();
      return;
    }
  }

  // Unknown command -> reply with error
  SerialBT.println("{\"ok\":false,\"error\":\"unknown_cmd\"}");
}

// --- Setup & loop ---
void setup() {
  Serial.begin(115200);
  delay(200);

  // Sensors
  dht.begin();
  analogReadResolution(12);
  analogSetPinAttenuation(MQ4_PIN, ADC_11db);
  pinMode(FLAME_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Motors
  pinMode(LEFT_MOTOR_FWD, OUTPUT);
  pinMode(LEFT_MOTOR_REV, OUTPUT);
  pinMode(RIGHT_MOTOR_FWD, OUTPUT);
  pinMode(RIGHT_MOTOR_REV, OUTPUT);
  stopMotors();

  // Init history
  for (int i = 0; i < HISTORY_SIZE; ++i) {
    history[i].t = 0;
    history[i].temp = NAN;
    history[i].hum = NAN;
    history[i].mq_adc = 0;
    history[i].mq_percent = 0;
    history[i].flame = false;
  }

  // Bluetooth
  if (!SerialBT.begin("FIRE-FIGHTER ROBOT")) {
    Serial.println("Bluetooth start failed!");
  } else {
    Serial.println("Bluetooth started: FIRE-FIGHTER ROBOT");
  }

  Serial.println("Setup complete.");
}

void loop() {
  // Read BT input (if any)
  if (SerialBT.available()) {
    // read line until newline or timeout
    String line = SerialBT.readStringUntil('\n');
    handleBluetoothCommand(line);
  }

  // --- NEW: periodic BT summary broadcast when client connected ---
  if (SerialBT.hasClient()) {
    unsigned long now = millis();
    if (now - lastBtBroadcast >= SAMPLE_INTERVAL) {
      lastBtBroadcast = now;
      sendSensorSummary(); // send compact summary every SAMPLE_INTERVAL (1s)
    }
  }

  // Auto-stop motors after timeout
  if (moving && (millis() - moveStartMillis >= MOVE_TIMEOUT)) {
    stopMotors();
    Serial.println("Movement timeout — motors stopped.");
  }

  // Periodic sensor sampling
  if (millis() - lastSampleMillis >= SAMPLE_INTERVAL) {
    lastSampleMillis = millis();
    sampleSensors();
  }
}
