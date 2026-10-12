/*
  Smart Garbage Cleaning Boat - Main Controller (ESP32)

  - 2 propeller motors via L298N  -> forward / backward / left / right
  - 1 conveyor belt motor via relay (separate battery) -> ON / OFF
  - WiFi web page with control buttons (+ live camera view from ESP32-CAM)

  Board in Arduino IDE: "ESP32 Dev Module"
  Library: only ESP32 core (WiFi.h, WebServer.h), no extra install needed.

  ================= WIRING (power OFF while connecting; common GND everywhere) =================
  ESP32 GPIO 27 -> L298N IN1        ESP32 GPIO 25 -> L298N IN3
  ESP32 GPIO 26 -> L298N IN2        ESP32 GPIO 33 -> L298N IN4
  ESP32 GPIO 14 -> L298N ENA        ESP32 GPIO 32 -> L298N ENB   (remove the ENA/ENB jumper caps)
  ESP32 GND     -> L298N GND -> motor battery (-)

  L298N OUT1/OUT2 -> Left propeller motor
  L298N OUT3/OUT4 -> Right propeller motor
  L298N 12V       -> motor battery (+)

  ESP32 GPIO 23 -> Relay IN         ESP32 5V/VIN -> Relay VCC       ESP32 GND -> Relay GND

  Conveyor motor (separate battery):
    Conveyor battery (+) -> Relay COM
    Relay NO             -> Conveyor motor terminal 1
    Conveyor battery (-) -> Conveyor motor terminal 2
  ===============================================================================================
  Full details with tables: see README.md
*/

#include <WiFi.h>
#include <WebServer.h>

// ---------- WiFi settings (apne badal lo) ----------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ESP32-CAM ka IP address (uske Serial Monitor me dikhta hai).
// Khali ("") chhodoge to control page pe video nahi dikhega.
const char* CAM_IP = "";

// ---------- Pin connections (apni wiring ke hisab se badlo) ----------
// L298N motor driver
#define ENA 14   // Left motor enable
#define IN1 27   // Left motor
#define IN2 26   // Left motor
#define IN3 25   // Right motor
#define IN4 33   // Right motor
#define ENB 32   // Right motor enable

// Relay module (conveyor motor)
#define RELAY_PIN 23
const bool RELAY_ACTIVE_LOW = true;  // Zyada tar relay modules active-LOW hote hain.
                                     // Agar relay ulta chale to false kar do.

// Safety: agar itne ms tak koi command na aaye to boat apne aap ruk jayegi
const unsigned long FAILSAFE_MS = 1000;

WebServer server(80);
unsigned long lastMoveCmd = 0;
bool moving = false;
bool conveyorOn = false;

// ---------- Motor functions ----------
void stopMotors() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  moving = false;
}

void forward() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  moving = true;
}

void backward() {
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
  moving = true;
}

// Left: left motor peeche, right motor aage (boat jagah pe ghoomti hai)
void turnLeft() {
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  moving = true;
}

// Right: left motor aage, right motor peeche
void turnRight() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
  moving = true;
}

void setConveyor(bool on) {
  conveyorOn = on;
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

// ---------- Web page ----------
const char PAGE_TOP[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Garbage Cleaning Boat</title>
<style>
  body { font-family: Arial, sans-serif; text-align: center; background: #101820; color: #fff; margin: 0; padding: 12px; }
  h2 { margin: 8px 0; }
  .cam { width: 100%; max-width: 480px; border-radius: 8px; margin-bottom: 10px; }
  .pad { display: grid; grid-template-columns: repeat(3, 90px); gap: 10px; justify-content: center; margin: 14px 0; }
  button { height: 70px; font-size: 18px; border: none; border-radius: 10px; background: #1f6feb; color: #fff; touch-action: none; user-select: none; }
  button:active { background: #0b4bb3; }
  .stop { background: #d73a49; }
  .conv { width: 200px; height: 56px; background: #2da44e; }
  .conv.off { background: #6e7781; }
</style>
</head>
<body>
<h2>Smart Garbage Cleaning Boat</h2>
)rawliteral";

const char PAGE_BOTTOM[] PROGMEM = R"rawliteral(
<div class="pad">
  <span></span><button data-d="F">&#9650;<br>Forward</button><span></span>
  <button data-d="L">&#9664;<br>Left</button>
  <button class="stop" data-d="S">STOP</button>
  <button data-d="R">Right<br>&#9654;</button>
  <span></span><button data-d="B">&#9660;<br>Back</button><span></span>
</div>
<button id="conv" class="conv off">Conveyor: OFF</button>
<script>
  let timer = null;
  function send(d) { fetch('/move?d=' + d).catch(() => {}); }
  function startHold(d) {
    send(d);
    if (d === 'S') return;
    clearInterval(timer);
    timer = setInterval(() => send(d), 300);  // jab tak button dabaya hai, command bhejta rahega
  }
  function endHold() { clearInterval(timer); timer = null; send('S'); }

  document.querySelectorAll('button[data-d]').forEach(b => {
    const d = b.dataset.d;
    b.addEventListener('pointerdown', e => { e.preventDefault(); startHold(d); });
    if (d !== 'S') {
      b.addEventListener('pointerup', endHold);
      b.addEventListener('pointerleave', () => { if (timer) endHold(); });
      b.addEventListener('pointercancel', endHold);
    }
  });

  let convOn = false;
  const cb = document.getElementById('conv');
  cb.addEventListener('click', () => {
    convOn = !convOn;
    fetch('/conv?s=' + (convOn ? 1 : 0)).catch(() => {});
    cb.textContent = 'Conveyor: ' + (convOn ? 'ON' : 'OFF');
    cb.classList.toggle('off', !convOn);
  });
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  String html = FPSTR(PAGE_TOP);
  if (strlen(CAM_IP) > 0) {
    html += "<img class='cam' src='http://" + String(CAM_IP) + ":81/stream'>";
  }
  html += FPSTR(PAGE_BOTTOM);
  server.send(200, "text/html", html);
}

void handleMove() {
  String d = server.arg("d");
  lastMoveCmd = millis();
  if      (d == "F") forward();
  else if (d == "B") backward();
  else if (d == "L") turnLeft();
  else if (d == "R") turnRight();
  else               stopMotors();
  server.send(200, "text/plain", "OK");
}

void handleConveyor() {
  setConveyor(server.arg("s") == "1");
  server.send(200, "text/plain", conveyorOn ? "ON" : "OFF");
}

void setup() {
  Serial.begin(115200);

  // Relay ko pehle OFF level pe set karo, phir pin output banao (startup pe relay click na kare)
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(RELAY_PIN, OUTPUT);

  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
  digitalWrite(ENA, HIGH);   // motors full speed pe enable
  digitalWrite(ENB, HIGH);
  stopMotors();
  setConveyor(false);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Open this in Chrome: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/conv", handleConveyor);
  server.begin();
}

void loop() {
  server.handleClient();

  // Failsafe: WiFi/phone se connection toote to boat ruk jaye
  if (moving && (millis() - lastMoveCmd > FAILSAFE_MS)) {
    stopMotors();
  }
}
