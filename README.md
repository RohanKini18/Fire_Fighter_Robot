
# Fire-Fighter Robot — ESP32 (Bluetooth + Wi-Fi Captive Portal)

This project implements a **fire-fighter robot** using an **ESP32**, with two control modes:

1. **Bluetooth mode** — for control and live sensor telemetry from an Android app.  
2. **Wi-Fi Captive Portal mode** — the ESP32 creates a Wi-Fi hotspot with a web-based controller and live dashboard.

Both modes use the same hardware pins for **motors, sensors, and buzzer**, but run as **separate Arduino sketches**.

---

## 1. Features

### Common Hardware Features

- Dual DC motor drive (left/right) with forward, reverse, left, right, and stop.
- **Flame sensor** for fire detection.
- **MQ-4 gas sensor** (CNG/LPG) with ADC percentage.
- **DHT11 sensor** for temperature & humidity.
- **Buzzer** alarm for flame detection and manual "FIRE" command.
- Circular **history buffer** (`HISTORY_SIZE = 120`) storing recent sensor readings.

### Bluetooth Mode (Android App)

- ESP32 exposes a classic Bluetooth device:
  - Name: **`FIRE-FIGHTER ROBOT`**
- Android app sends simple **text commands**:
  - `forward`, `back`, `left`, `right`, `stop`, `fire`
  - `get_sensor` / `get` / `sensor` → replies with JSON (current + history)
  - `ping` → reply: `{"ok":true,"reply":"pong"}`
- Periodic **1 second broadcast** of **compact JSON summary** while a BT client is connected.

### Wi-Fi Captive Portal Mode

- ESP32 runs as **Wi-Fi SoftAP**:
  - SSID: **`FIRE-FIGHTER ROBOT`**
  - Password: **`NMAMIT2026`** (must be ≥ 8 chars; if shorter, AP becomes open)
- Starts **captive portal style** DNS:
  - Any domain resolves to ESP32 AP IP.
- HTTP server with pages:
  - `/` — **Dashboard landing page** (FF Portal):
    - Live temperature, humidity, MQ-4 %, flame status.
    - Live raw JSON payload view.
    - Buttons:
      - **Controls** → `/controls`
      - **See Details** → `/details`
  - `/controls` — **Gamepad-style control page**:
    - On-screen buttons: ↑, ↓, ←, →, FIRE.
    - Keyboard control:
      - Arrow keys → motion.
      - Spacebar → FIRE.
  - `/details` — **Sensor table view**:
    - Live table from history buffer with ▲ / ▼ arrows.
  - `/sensor` — JSON API for current state + history.
  - `/control?cmd=...` — HTTP control API.

---

## 2. Hardware Setup

### Components

- ESP32 Dev Board
- 2x DC motors + motor driver (L298N, L293D, or similar)
- DHT11 temperature & humidity sensor
- MQ-4 gas sensor (analog)
- Flame sensor (digital)
- Buzzer
- Power supply appropriate for motors + ESP32
- Connecting wires, breadboard / PCB, chassis, etc.

> **Note:** Pin numbers below are ESP32 GPIO numbers.

### Pin Mapping (Common to Both Sketches)

#### Sensors

- `DHTPIN` = **14** → DHT11 data pin.
- `MQ4_PIN` = **34** → MQ-4 analog output (to ADC).
- `FLAME_PIN` = **35** → flame sensor digital output.
- `BUZZER_PIN` = **25** → buzzer control pin.

> `ACTIVE_LOW_FLAME`  
> - `false` → flame is detected when input is **HIGH**.  
> - `true` → flame is detected when input is **LOW**.  
> Set this according to your flame sensor module.

#### Motors

```cpp
const int LEFT_MOTOR_FWD  = 32;
const int LEFT_MOTOR_REV  = 33;
const int RIGHT_MOTOR_FWD = 26;
const int RIGHT_MOTOR_REV = 27;
```

Wire these pins to your motor driver’s IN1/IN2/IN3/IN4 (or equivalent) inputs.

---

## 3. Code Files Overview

You have **two separate Arduino sketches**:

1. **`fire_fighter_robot_bluetooth.ino`**  _(BluetoothSerial version)_  
   - Uses:
     - `BluetoothSerial.h`
     - `DHT.h`
     - `ArduinoJson.h`
   - Provides:
     - Bluetooth motor control.
     - JSON telemetry for Android app.
     - Periodic summary broadcasts.

2. **`fire_fighter_robot_wifi_captive.ino`**  _(Wi-Fi captive portal version)_  
   - Uses:
     - `WiFi.h`
     - `WebServer.h`
     - `DNSServer.h`
     - `DHT.h`
   - Provides:
     - SoftAP + HTTP UI + JSON APIs.
     - Captive-style DNS redirection.

> You **cannot** run both modes at the same time on a single ESP32.  
> Flash **only one sketch** at a time depending on your use case.

---

## 4. Arduino IDE Setup

### 4.1. Install ESP32 Board Package

1. In Arduino IDE, go to **File → Preferences**.
2. In *Additional Boards Manager URLs*, add:

   ```text
   https://dl.espressif.com/dl/package_esp32_index.json
   ```

3. Go to **Tools → Board → Boards Manager...**
4. Search **“ESP32 by Espressif Systems”** and install.

Select your board:  
> **Tools → Board → ESP32 Arduino → [Your ESP32 Board]**

### 4.2. Required Libraries

Install the following via **Sketch → Include Library → Manage Libraries...**:

- **DHT sensor library** (Adafruit)
- **Adafruit Unified Sensor** (dependency)
- **ArduinoJson** (for Bluetooth sketch)

`BluetoothSerial`, `WiFi`, `WebServer`, and `DNSServer` come with the ESP32 core.

---

## 5. Bluetooth Mode — Usage

### 5.1. Flash the Bluetooth Sketch

- Open `fire_fighter_robot_bluetooth.ino` (your first code block).
- Select correct **Board** and **Port**.
- **Upload**.

### 5.2. Pairing

- Power the robot.
- On Android:
  - Open Bluetooth settings.
  - Pair with device named **`FIRE-FIGHTER ROBOT`**.
- Your Android app should open a **BluetoothSerial** connection to this device.

### 5.3. Commands from Android App

Send plain **text lines** ending with `\n` (newline). For example:

| Command       | Description                          |
|--------------|--------------------------------------|
| `forward`    | Move forward (auto stop after 500ms) |
| `back`       | Move backward                        |
| `left`       | Turn left                            |
| `right`      | Turn right                           |
| `stop`       | Stop all motors                      |
| `fire`       | Trigger buzzer for ~300ms            |
| `get_sensor` | Get full JSON with history           |
| `get`        | Alias for `get_sensor`               |
| `sensor`     | Alias for `get_sensor`               |
| `ping`       | Returns `{"ok":true,"reply":"pong"}` |

Also supported, HTTP-like strings over BT (if your app uses them):

- `GET /control?cmd=forward`
- `GET /sensor`

These get parsed and internally mapped to the same commands.

### 5.4. Telemetry JSON Format

**Full payload** (for `get_sensor`):

```json
{
  "ok": true,
  "timestamp": 123456,
  "temperature": 28.5,
  "humidity": 65.0,
  "mq_percent": 12.34,
  "mq_adc": 507,
  "flame": false,
  "history": [
    {
      "t": 1000,
      "temp": 28.1,
      "hum": 64.2,
      "mq_percent": 11.23,
      "mq_adc": 478,
      "flame": false
    }
  ]
}
```

**Periodic compact summary** (sent every 1s while client is connected):

```json
{
  "ok": true,
  "timestamp": 123456,
  "temperature": 28.50,
  "humidity": 65.10,
  "mq_percent": 12.34,
  "mq_adc": 507,
  "flame": false
}
```

Use this in your Android app for **live graphs** and **dashboard**.

---

## 6. Wi-Fi Captive Portal Mode — Usage

### 6.1. Flash the Wi-Fi Sketch

- Open `fire_fighter_robot_wifi_captive.ino` (your second code block).
- Verify `AP_SSID` and `AP_PASS`:

```cpp
const char* AP_SSID = "FIRE-FIGHTER ROBOT";
const char* AP_PASS = "NMAMIT2026";
```

- Upload to ESP32.

### 6.2. Connect from Phone / Laptop

1. On your phone/laptop, open Wi-Fi settings.
2. Connect to **`FIRE-FIGHTER ROBOT`** (password `NMAMIT2026`).
3. In many cases, the captive portal will auto-open. If not:
   - Open a browser and go to any URL (e.g. `http://example.com`); the DNS will redirect to the ESP32.
   - Or visit `http://192.168.4.1/` directly.

### 6.3. Web Pages

#### `/` — FF Portal (Dashboard)

- Shows:
  - Temperature (°C)
  - Humidity (%)
  - MQ-4 gas percentage (% of ADC)
  - Flame status (No Flame / Flame Detected!)
  - Raw JSON under **“Waiting for data...” / live JSON**.
- Periodically (every 1s) calls `/sensor` via JavaScript and updates the UI.
- Has `Controls` and `See Details` buttons:
  - `Controls` → `/controls`
  - `See Details` → `/details`
- Intro overlay:
  - Shows project name, developer names:
    - **ROHAN T KINI - NNM22CS187**
    - **SPOORTHI S KOTIAN - NNM22CS178**

#### `/controls` — Gamepad-Style Control

- Big **↑, ↓, ←, →, FIRE** buttons.
- Each button calls `/control?cmd=forward/back/left/right/fire`.
- Keyboard shortcuts:
  - **Arrow Up** → forward
  - **Arrow Down** → back
  - **Arrow Left** → left
  - **Arrow Right** → right
  - **Spacebar** → FIRE

#### `/details` — Sensor History Table

- Table columns:
  - Time
  - Temp (°C)
  - Hum (%)
  - CNG % (MQ-4)
  - MQ ADC
  - Flame (0/1)
- Uses arrows:
  - ▲ if value increased from previous row.
  - ▼ if value decreased.
- Fetches data from `/sensor` every 1s.

### 6.4. HTTP APIs (for Custom Clients)

#### GET `/sensor`

Returns JSON (including full history):

```json
{
  "ok": true,
  "timestamp": 123456,
  "temperature": 28.50,
  "humidity": 65.10,
  "mq_adc": 507,
  "mq_percent": 12.34,
  "flame": false,
  "history": [
    {
      "t": 1000,
      "temp": 28.10,
      "hum": 64.20,
      "mq_percent": 11.23,
      "mq_adc": 478,
      "flame": false
    }
  ]
}
```

#### GET `/control?cmd=<command>`

Valid `cmd` values:

- `forward`
- `back`
- `left`
- `right`
- `stop`
- `fire`

Example response:

```json
{"ok":true,"cmd":"forward"}
```

Errors:

```json
{"ok":false,"error":"missing_cmd"}
{"ok":false,"error":"unknown_cmd"}
```

---

## 7. Timings & Safety

- **Movement timeout**:  
  `MOVE_TIMEOUT = 500` ms  
  → Robot moves in **short pulses**, then auto-stops to avoid runaway.

- **Sampling interval**:  
  `SAMPLE_INTERVAL = 1000` ms  
  → Sensors are read every 1 second.

- **Buzzer rate limit**:  
  `BUZZER_MIN_INTERVAL = 1000` ms  
  → Buzzer will not repeat faster than once per second (prevents continuous beeping).

- **Flame auto-alarm**:
  - If `flameNow` is detected, buzzer is triggered (for 150–300 ms) AND a log is printed over Serial.

---

## 8. Debugging Tips

- Open **Serial Monitor** at **115200 baud** to see:
  - Wi-Fi AP IP (`AP IP: 192.168.4.1`).
  - Flame detection logs.
  - Sample logs:
    - Bluetooth sketch:  
      `Sample: T=28.5 H=65.0 MQ=507 (12.4%) Flame=0`
    - Wi-Fi sketch:  
      `t:28.5C h:65.0% mq:507 (12.4%) flame:0`
- If sensors show **0 or NAN**, check:
  - DHT wiring (VCC, GND, DATA with proper pull-up).
  - MQ-4 analog output connected to GPIO 34.
  - Flame sensor output polarity and `ACTIVE_LOW_FLAME`.

---

## 9. Notes / Customization

- You can adjust:
  - `HISTORY_SIZE` (more or less samples).
  - `MOVE_TIMEOUT` for longer/shorter movement pulses.
  - `SAMPLE_INTERVAL` for faster or slower updates.
  - `AP_SSID` and `AP_PASS` to customize Wi-Fi network.
- For production:
  - Add **battery monitoring**, **obstacle sensors**, or **water pump** control logic if using actual fire-fighting hardware.

---

## 10. Credits

- **Project:** Fire-Fighter Robot  
- **Developers:**  
  - **Rohan T Kini — NNM22CS187**  
  - **Spoorthi S Kotian — NNM22CS178**
