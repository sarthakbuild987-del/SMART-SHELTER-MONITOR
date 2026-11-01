/*
  Smart Emergency Shelter Occupancy Monitor - ESP32 firmware (Phase 2)

  Two IR sensors at the doorway count people in and out.
  Count, temperature, humidity and fire status go to Firebase every time
  something changes, and at least every 30 seconds.

  Wiring:
    IR outer sensor (outside of door)  -> GPIO 32
    IR inner sensor (inside of door)   -> GPIO 33
    Flame sensor AO                    -> GPIO 34
    DHT22 data                         -> GPIO 4
    OLED SDA / SCL                     -> GPIO 21 / 22
    Active buzzer (+)                  -> GPIO 27

  Direction: outer sensor then inner sensor = ENTRY (+1)
             inner sensor then outer sensor = EXIT  (-1)
*/

// Set USE_OLED to 0 if you did not buy the 0.96" OLED display
#define USE_OLED 1

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#if USE_OLED
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif
#include <DHT.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASS, DB_URL, FIREBASE_API_KEY, DEVICE_EMAIL, DEVICE_PASS

// ---------- Settings ----------
const char* SHELTER_ID = "shelter1";   // must match the key in Firebase
const int   CAPACITY   = 50;

// ---------- Pins ----------
#define PIN_IR_OUTER 32
#define PIN_IR_INNER 33
#define PIN_FLAME_AO 34
#define PIN_DHT       4
#define PIN_BUZZER   27

// IR obstacle modules (and Wokwi buttons wired to GND) read LOW when triggered
#define SENSOR_ACTIVE LOW

// Flame module analog value drops when a flame is seen. Calibrate this.
const int FLAME_THRESHOLD = 1500;

// ---------- Timing ----------
// 5000 is easy for mouse clicks in Wokwi. Use about 1500 on a real doorway.
const unsigned long PAIR_TIMEOUT_MS   = 5000;   // time allowed between the two sensors
const unsigned long CLEAR_MS          = 250;    // both sensors must be free this long
const unsigned long HEARTBEAT_MS      = 30000;
const unsigned long MIN_UPLOAD_GAP_MS = 1500;

// ---------- Objects ----------
#if USE_OLED
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
#endif
DHT dht(PIN_DHT, DHT22);

// ---------- Shared state ----------
volatile int   count = 0;
volatile bool  dirty = true;          // something changed, needs upload
volatile bool  fire = false;
volatile float temperature = NAN;
volatile float humidity = NAN;
volatile bool  wifiOk = false;
volatile bool  lastUploadOk = false;

// Firebase login token (used only by the upload task)
String idToken = "";
unsigned long tokenAt = 0;

// ---------- Buzzer (non-blocking) ----------
int beepsLeft = 0;
bool beepOn = false;
unsigned long beepT = 0;

void startBeeps(int n) { beepsLeft = n; beepOn = false; beepT = 0; }

void updateBuzzer() {
  if (fire) { digitalWrite(PIN_BUZZER, (millis() / 250) % 2); return; }
  if (beepsLeft <= 0) { digitalWrite(PIN_BUZZER, LOW); return; }
  if (millis() - beepT >= 150) {
    beepT = millis();
    beepOn = !beepOn;
    digitalWrite(PIN_BUZZER, beepOn);
    if (!beepOn) beepsLeft--;
  }
}

// ---------- Counting ----------
enum State { IDLE, OUTER_FIRST, INNER_FIRST, WAIT_CLEAR };
State state = IDLE;
unsigned long stateT = 0;
unsigned long clearSince = 0;

void changeCount(int delta) {
  int old = count;
  int now = old + delta;
  if (now < 0) now = 0;
  if (now == old) return;
  count = now;
  if (now >= CAPACITY && old < CAPACITY) startBeeps(3);                 // just became full
  else if (now * 10 >= CAPACITY * 8 && old * 10 < CAPACITY * 8) startBeeps(1);  // reached 80%
  dirty = true;
  Serial.printf("Count: %d / %d\n", now, CAPACITY);
}

void runCounter() {
  bool outer = digitalRead(PIN_IR_OUTER) == SENSOR_ACTIVE;
  bool inner = digitalRead(PIN_IR_INNER) == SENSOR_ACTIVE;
  unsigned long now = millis();

  switch (state) {
    case IDLE:
      if (outer)      { state = OUTER_FIRST; stateT = now; }
      else if (inner) { state = INNER_FIRST; stateT = now; }
      break;
    case OUTER_FIRST:
      if (inner)                          { changeCount(+1); state = WAIT_CLEAR; clearSince = 0; }
      else if (now - stateT > PAIR_TIMEOUT_MS) { state = WAIT_CLEAR; clearSince = 0; }
      break;
    case INNER_FIRST:
      if (outer)                          { changeCount(-1); state = WAIT_CLEAR; clearSince = 0; }
      else if (now - stateT > PAIR_TIMEOUT_MS) { state = WAIT_CLEAR; clearSince = 0; }
      break;
    case WAIT_CLEAR:
      if (outer || inner) clearSince = 0;
      else if (clearSince == 0) clearSince = now;
      else if (now - clearSince >= CLEAR_MS) state = IDLE;
      break;
  }
}

// ---------- Firebase login + upload (REST) ----------
// The device signs in with its own account. The database rules only let
// this account write count, temp, humidity, flame and updated.
bool signIn() {
  WiFiClientSecure client;
  client.setInsecure();   // fine for a prototype; use a certificate in production
  client.setTimeout(20);
  if (!client.connect("identitytoolkit.googleapis.com", 443)) {
    Serial.println("Sign-in: connect failed");
    return false;
  }
  String body = String("{\"email\":\"") + DEVICE_EMAIL + "\",\"password\":\"" + DEVICE_PASS + "\",\"returnSecureToken\":true}";
  client.print(String("POST /v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY + " HTTP/1.0\r\n" +
               "Host: identitytoolkit.googleapis.com\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " + body.length() + "\r\n\r\n" + body);
  String status = client.readStringUntil('\n');
  int code = status.substring(9, 12).toInt();
  while (client.connected()) { String line = client.readStringUntil('\n'); if (line == "\r") break; }
  String resp = client.readString();
  client.stop();
  if (code != 200) { Serial.printf("Sign-in failed: HTTP %d\n", code); Serial.println(resp); return false; }

  int k = resp.indexOf("\"idToken\"");
  if (k < 0) return false;
  int a = resp.indexOf('"', resp.indexOf(':', k) + 1) + 1;
  int b = resp.indexOf('"', a);
  if (a <= 0 || b <= a) return false;
  idToken = resp.substring(a, b);
  tokenAt = millis();
  Serial.println("Signed in to Firebase");
  return true;
}

bool sendToFirebase() {
  // Tokens last 1 hour; sign in again after 45 minutes
  if (idToken.length() == 0 || millis() - tokenAt > 2700000UL) {
    if (!signIn()) return false;
    vTaskDelay(50 / portTICK_PERIOD_MS);   // let other tasks run between the two handshakes
  }

  int c = count;
  bool f = fire;
  float t = temperature, h = humidity;

  char body[240];
  int n = snprintf(body, sizeof(body),
    "{\"count\":%d,\"flame\":%s,\"updated\":{\".sv\":\"timestamp\"}",
    c, f ? "true" : "false");
  if (!isnan(t)) n += snprintf(body + n, sizeof(body) - n, ",\"temp\":%.1f", t);
  if (!isnan(h)) n += snprintf(body + n, sizeof(body) - n, ",\"humidity\":%.1f", h);
  snprintf(body + n, sizeof(body) - n, "}");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20);
  HTTPClient http;
  http.setConnectTimeout(20000);
  http.setTimeout(20000);
  String url = String(DB_URL) + "/shelters/" + SHELTER_ID + ".json?auth=" + idToken;
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(body);
  http.end();
  Serial.printf("Upload -> HTTP %d\n", code);
  if (code == 401) idToken = "";   // token rejected: sign in again next time
  return code == 200;
}

void uploadTask(void*) {
  unsigned long lastSend = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiOk = false;
      WiFi.reconnect();
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    }
    wifiOk = true;
    unsigned long now = millis();
    bool due = dirty || lastSend == 0 || (now - lastSend >= HEARTBEAT_MS);
    if (due && (lastSend == 0 || now - lastSend >= MIN_UPLOAD_GAP_MS)) {
      dirty = false;                    // cleared first, so changes during upload are not lost
      bool ok = sendToFirebase();
      lastUploadOk = ok;
      if (!ok) { dirty = true; vTaskDelay(8000 / portTICK_PERIOD_MS); }   // retry after a pause
      lastSend = millis();
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// ---------- Display ----------
#if USE_OLED
void drawScreen() {
  int c = count;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(SHELTER_ID);
  oled.setCursor(84, 0);
  oled.print(wifiOk ? (lastUploadOk ? "Cloud OK" : "Sending") : "No WiFi");

  oled.setTextSize(3);
  oled.setCursor(0, 14);
  oled.print(c);
  oled.setTextSize(1);
  oled.print(" / ");
  oled.print(CAPACITY);

  oled.setCursor(0, 44);
  if (fire)                      oled.print("!! FIRE DETECTED !!");
  else if (c >= CAPACITY)        oled.print("SHELTER FULL");
  else if (c * 10 >= CAPACITY * 8) oled.print("Nearly full");
  else                           oled.print("Space available");

  oled.setCursor(0, 56);
  float t = temperature, h = humidity;
  if (!isnan(t)) { oled.print(t, 1); oled.print("C  "); }
  if (!isnan(h)) { oled.print(h, 0); oled.print("%"); }
  oled.display();
}
#else
void drawScreen() {}   // no display fitted
#endif

// ---------- Setup / loop ----------
// Opens a plain secure connection first, which helps the slow Wokwi network wake up.
void preconnect(const char* host) {
  WiFiClientSecure c;
  c.setInsecure();
  c.setTimeout(20);
  bool ok = c.connect(host, 443);
  Serial.printf("preconnect %s: %s\n", host, ok ? "OK" : "FAILED");
  c.stop();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_IR_OUTER, INPUT_PULLUP);
  pinMode(PIN_IR_INNER, INPUT_PULLUP);
  pinMode(PIN_FLAME_AO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

#if USE_OLED
  Wire.begin(21, 22);
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Shelter monitor");
  oled.println("Connecting WiFi...");
  oled.display();
#endif

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);   // channel 6 is for Wokwi only. On a real board use WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) delay(250);
  Serial.printf("WiFi status=%d ip=%s\n", WiFi.status(), WiFi.localIP().toString().c_str());
  preconnect("smart-shelter-monitor-default-rtdb.asia-southeast1.firebasedatabase.app");
  preconnect("identitytoolkit.googleapis.com");

  // Core 1 (not core 0): TLS handshakes are CPU heavy and would trip the core 0 watchdog
  xTaskCreatePinnedToCore(uploadTask, "upload", 16384, NULL, 1, NULL, 1);
}

void loop() {
  static unsigned long lastEnv = 0, lastFlame = 0, lastDraw = 0;
  unsigned long now = millis();

  runCounter();
  updateBuzzer();

  if (now - lastFlame >= 200) {
    lastFlame = now;
    bool f = analogRead(PIN_FLAME_AO) < FLAME_THRESHOLD;
    if (f != fire) { fire = f; dirty = true; }
  }

  if (now - lastEnv >= 5000) {
    lastEnv = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) temperature = t;
    if (!isnan(h)) humidity = h;
  }

  if (now - lastDraw >= 500) {
    lastDraw = now;
    drawScreen();
  }

  delay(5);
}
