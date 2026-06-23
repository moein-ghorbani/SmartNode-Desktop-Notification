/*
  Ultimate Smart Notification Node & Desk Companion + Micro Web Dashboard (STABLE & LIGHTWEIGHT)
  ESP32-S2 Mini + WS2812 RGB Ring + OLED SSD1306 + Buzzer + NTP Clock
*/

#include <WiFi.h>
#include <WebServer.h>
#include <FastLED.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "time.h"
#include "driver/rtc_io.h"

// ================= CONFIGURATION & CONSTANTS =================
#define API_TOKEN          "SECURE_NODE_TOKEN_1366"
#define WDT_TIMEOUT_SECONDS 10
#define TELEMETRY_INTERVAL  60000
#define IDLE_TO_CLOCK_TIME  60000 

// ================= GPIO PINS =================
#define LED_PIN     18
#define LED_COUNT   12
#define BUZZER_PIN  17
#define BUTTON_PIN  21 

// ================= OLED =================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= RGB LEDS =================
CRGB leds[LED_COUNT];
#define DEFAULT_BRIGHTNESS 30

// ================= NETWORK SERVICES =================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);
Preferences preferences;

const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic_sub = "smartnode/notify";
const char* mqtt_topic_pub = "smartnode/telemetry";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 12600; 
const int   daylightOffset_sec = 0;

// ================= STATE MANAGEMENT =================
String level = "IDLE";
String title = "READY";
String message = "System Live";

String lastLevel = "IDLE";
String lastTitle = "READY";
String lastMessage = "System Live";

CRGB customColor = CRGB::Green;
int customBlinkSpeed = 60;
int customBeepCount = 0;
int beepCounter = 0;

bool muted = false;
bool hasUnread = false;
bool pomodoroMode = false;
unsigned long pomodoroStart = 0;

unsigned long lastAnimation = 0;
unsigned long lastBuzzerAction = 0;
unsigned long buttonDown = 0;
unsigned long lastTelemetry = 0;
unsigned long lastActionTime = 0; 
int scrollOffset = 0;
unsigned long lastScrollTime = 0;

// ================= OLED DRIVER =================
void oledShow() {
  display.clearDisplay();
  
  if (pomodoroMode) {
    unsigned long elapsed = (millis() - pomodoroStart) / 1000;
    long remaining = (25 * 60) - elapsed;
    if (remaining <= 0) {
      pomodoroMode = false;
      level = "INFO"; title = "FOCUS DONE"; message = "Take a break!";
      tone(BUZZER_PIN, 1000, 1000);
      lastActionTime = millis();
    } else {
      display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
      display.setCursor(25, 10); display.println("FOCUS SESSION");
      display.setTextSize(2); display.setCursor(35, 30);
      char timerStr[6]; sprintf(timerStr, "%02ld:%02ld", remaining / 60, remaining % 60);
      display.println(timerStr);
    }
  }
  else if ((level == "IDLE" || level == "INFO") && (millis() - lastActionTime > IDLE_TO_CLOCK_TIME)) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      static int matrixX = random(0, 128); static int matrixY = 0;
      display.drawPixel(matrixX, matrixY, SSD1306_WHITE);
      matrixY += 4; if (matrixY > 64) { matrixY = 0; matrixX = random(0, 128); }

      display.setTextSize(2); display.setTextColor(SSD1306_WHITE); display.setCursor(16, 16);
      char timeHourMin[6]; strftime(timeHourMin, sizeof(timeHourMin), "%H:%M", &timeinfo);
      display.println(timeHourMin);

      int secStr = timeinfo.tm_sec; display.fillRect(0, 62, map(secStr, 0, 59, 0, 128), 2, SSD1306_WHITE);
      display.setTextSize(1); display.setCursor(16, 42);
      char timeDate[12]; strftime(timeDate, sizeof(timeDate), "%Y-%m-%d", &timeinfo);
      display.print(timeDate);
    } else {
      display.setTextSize(1); display.setCursor(0, 20); display.println("Syncing Time...");
    }
  } 
  else {
    display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0, 0);
    display.print("["); display.print(level); display.print("]");
    display.setCursor(0, 16); display.print("T: "); display.println(title);
    display.setCursor(0, 36); display.print("M: ");
    if (message.length() > 14) {
      String scrolledMsg = message + "   " + message;
      display.println(scrolledMsg.substring(scrollOffset, scrollOffset + 14));
      if (millis() - lastScrollTime > 350) {
        scrollOffset++; if (scrollOffset >= message.length() + 3) scrollOffset = 0;
        lastScrollTime = millis();
      }
    } else { display.println(message); }
    display.setCursor(0, 56); display.print(WiFi.localIP().toString());
    if (mqttClient.connected()) display.print(" | MQTT");
  }
  display.display();
}

// ================= GO TO DEEP SLEEP ROUTINE =================
void enterDeepSleep() {
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 25); display.println("Going to Sleep..."); display.display();

  for (int i = LED_COUNT - 1; i >= 0; i--) { leds[i] = CRGB::Black; FastLED.show(); delay(80); }
  display.clearDisplay(); display.display();

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  gpio_pullup_en((gpio_num_t)BUTTON_PIN);
  gpio_pulldown_dis((gpio_num_t)BUTTON_PIN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
  delay(100); 
  esp_deep_sleep_start();
}

// ================= NON-BLOCKING BUZZER ENGINE =================
void handleBuzzer() {
  if (muted || pomodoroMode) return;
  unsigned long currentMillis = millis();
  
  if (level == "CRITICAL" && currentMillis - lastBuzzerAction > 400) {
    lastBuzzerAction = currentMillis; tone(BUZZER_PIN, 2200, 150);
  } else if (level == "WARNING" && currentMillis - lastBuzzerAction > 1500) {
    lastBuzzerAction = currentMillis; tone(BUZZER_PIN, 1300, 100);
  } else if (level == "CUSTOM" && customBeepCount > 0 && beepCounter < customBeepCount) {
    if (currentMillis - lastBuzzerAction > 300) {
      lastBuzzerAction = currentMillis; tone(BUZZER_PIN, 1800, 120); beepCounter++;
    }
  }
}

// ================= RGB SUB-ANIMATIONS =================
void breathing(CRGB color, int maxB = 45) {
  static int brightness = 5; static int dir = 1; brightness += dir;
  if (brightness >= maxB || brightness <= 5) dir = -dir;
  fill_solid(leds, LED_COUNT, color); FastLED.setBrightness(brightness);
}

void warningPulse() {
  static int b = 10; static int d = 2; b += d;
  if (b > 75 || b < 10) d = -d;
  fill_solid(leds, LED_COUNT, CRGB::Orange); FastLED.setBrightness(b);
}

void infoAnimation() {
  static uint8_t hue = 0;
  for (int i = 0; i < LED_COUNT; i++) leds[i] = CHSV(hue + i * 20, 255, 160);
  hue += 3; FastLED.setBrightness(DEFAULT_BRIGHTNESS);
}

void deskAmbientClockLight() {
  static uint8_t baseHue = 140; static unsigned long lastHueShift = 0;
  if (millis() - lastHueShift > 60000) { baseHue += 10; lastHueShift = millis(); }
  static int ambientB = 10; static int ambientDir = 1; ambientB += ambientDir;
  if (ambientB >= 25 || ambientB <= 5) ambientDir = -ambientDir;
  for (int i = 0; i < LED_COUNT; i++) leds[i] = CHSV(baseHue + (i * 2), 240, 120);
  if (hasUnread) {
    static bool blinkState = false; static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 1000) { blinkState = !blinkState; lastBlink = millis(); }
    if (blinkState) leds[0] = CRGB::Red;
  }
  FastLED.setBrightness(ambientB);
}

// ================= RGB CORE ENGINE =================
void updateRGB() {
  if (pomodoroMode) {
    unsigned long elapsed = (millis() - pomodoroStart) / 1000;
    int activeLeds = map(elapsed, 0, 25 * 60, LED_COUNT, 0);
    FastLED.clear();
    for (int i = 0; i < activeLeds; i++) leds[i] = CRGB::Cyan;
    FastLED.setBrightness(20);
  }
  else if ((level == "IDLE" || level == "INFO") && (millis() - lastActionTime > IDLE_TO_CLOCK_TIME)) {
    deskAmbientClockLight();
  } else {
    if (level == "CRITICAL") {
      static bool on = false; on = !on;
      if (on) { fill_solid(leds, LED_COUNT, CRGB::Red); FastLED.setBrightness(70); } else { FastLED.clear(); }
    }
    else if (level == "WARNING") warningPulse();
    else if (level == "INFO") infoAnimation();
    else if (level == "CUSTOM") breathing(customColor);
    else breathing(CRGB::Green);
  }
  FastLED.show();
}

// ================= PARSE NOTIFICATION CORE =================
bool processNotificationPayload(String body) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) return false;

  const char* token = doc["token"] | "";
  if (strcmp(token, API_TOKEN) != 0) return false;

  pomodoroMode = false; level = doc["level"] | "IDLE";
  title = doc["title"] | "Notification"; message = doc["message"] | "";
  
  if (level == "CUSTOM") {
    const char* hexColor = doc["color"] | "#0000FF"; customColor = strtol(hexColor + 1, NULL, 16);
    customBlinkSpeed = doc["speed"] | 60; customBeepCount = doc["beeps"] | 0; beepCounter = 0;
  }

  lastLevel = level; lastTitle = title; lastMessage = message;
  muted = false; hasUnread = (level == "CRITICAL" || level == "WARNING");
  scrollOffset = 0; lastActionTime = millis(); 
  
  preferences.begin("notif-node", false);
  preferences.putString("level", level); preferences.putString("title", title); preferences.putString("message", message);
  preferences.end();
  
  oledShow();
  return true;
}

// ================= STREAM BASED LIGHTWEIGHT WEB RESPONSES =================
void handleRootDashboard() {
  esp_task_wdt_reset();
  // ارسال تکه تکه بدنه HTML جهت جلوگیری از کرش رم و سرریز بافر پردازنده
  server.sendHeader("Content-Type", "text/html; charset=utf-8");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  
  server.sendContent("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>SmartNode</title>");
  server.sendContent("<style>body{font-family:Arial;background:#121824;color:#fff;text-align:center;padding:30px;}");
  server.sendContent(".btn{background:#0284c7;color:#fff;border:none;padding:12px 20px;margin:10px;border-radius:5px;cursor:pointer;font-weight:bold;}");
  server.sendContent(".danger{background:#ef4444;}</style></head><body>");
  server.sendContent("<h2>🎛️ کنترل سریع SmartNode</h2>");
  
  // دکمه‌های کنترلی مستقیم
  server.sendContent("<button class='btn' onclick=\"location.href='/api/action?type=mute'\">🔕 سایلنت دستگاه</button>");
  server.sendContent("<button class='btn' onclick=\"location.href='/api/action?type=pomodoro'\">🎯 پومودورو</button><br>");
  server.sendContent("<button class='btn danger' onclick=\"location.href='/api/action?type=sleep'\">💤 خواب عمیق</button>");
  
  // نمایش تلمتری مستقیم
  server.sendContent("<br><br><div style='background:#1e293b;padding:15px;display:inline-block;border-radius:8px;'>");
  server.sendContent("<p>Uptime: " + String(millis() / 1000) + " s</p>");
  server.sendContent("<p>Free Heap: " + String(ESP.getFreeHeap()) + " Bytes</p>");
  server.sendContent("<p>RSSI: " + String(WiFi.RSSI()) + " dBm</p>");
  server.sendContent("<p>Level: " + level + "</p></div></body></html>");
  server.sendContent(""); 
}

void handleActionAPI() {
  String actionType = server.arg("type");
  lastActionTime = millis();
  
  if (actionType == "mute") {
    muted = true; hasUnread = false; level = "IDLE"; title = "MUTED"; message = "Cleared"; scrollOffset = 0;
  } else if (actionType == "pomodoro") {
    pomodoroMode = true; pomodoroStart = millis();
  } else if (actionType == "sleep") {
    server.send(200, "text/html", "Going to sleep mode...");
    delay(500);
    enterDeepSleep();
    return;
  }
  oledShow();
  server.send(200, "text/html", "<script>alert('Done!');window.location.href='/';</script>");
}

void handleNotifyRoute() {
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"status\":\"error\"}"); return; }
  if (processNotificationPayload(server.arg("plain"))) { server.send(200, "application/json", "{\"status\":\"success\"}"); }
  else { server.send(401, "application/json", "{\"status\":\"unauthorized\"}"); }
}

// ================= MQTT SERVICE ROUTINES =================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String body = "";
  for (unsigned int i = 0; i < length; i++) body += (char)payload[i];
  processNotificationPayload(body);
}

void reconnectMQTT() {
  if (!mqttClient.connected()) {
    String clientId = "ESP32S2-Node-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      mqttClient.subscribe(mqtt_topic_sub);
    }
  }
}

void sendTelemetry() {
  if (millis() - lastTelemetry > TELEMETRY_INTERVAL) {
    lastTelemetry = millis();
    if (mqttClient.connected()) {
      StaticJsonDocument<256> telemetryDoc;
      telemetryDoc["uptime"] = millis() / 1000;
      telemetryDoc["rssi"] = WiFi.RSSI();
      telemetryDoc["free_heap"] = ESP.getFreeHeap();
      
      char buffer[256]; serializeJson(telemetryDoc, buffer);
      mqttClient.publish(mqtt_topic_pub, buffer);
    }
  }
}

// ================= ADVANCED BUTTON CONTROL =================
void checkButton() {
  static bool pressed = false;
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!pressed) { pressed = true; buttonDown = millis(); }
    else if (millis() - buttonDown > 5000) { leds[0] = CRGB::Red; FastLED.show(); }
  } else {
    if (pressed) {
      unsigned long duration = millis() - buttonDown; pressed = false; lastActionTime = millis(); 
      if (duration >= 5000) enterDeepSleep();
      else if (duration >= 2000 && duration < 5000) { pomodoroMode = !pomodoroMode; if (pomodoroMode) pomodoroStart = millis(); oledShow(); }
      else if (duration > 50 && duration < 2000) {
        if (pomodoroMode) pomodoroMode = false;
        else { muted = true; hasUnread = false; level = "IDLE"; title = "MUTED"; message = "Cleared"; scrollOffset = 0; }
        oledShow();
      }
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT); pinMode(BUTTON_PIN, INPUT_PULLUP);

  esp_task_wdt_config_t wdt_config = { .timeout_ms = WDT_TIMEOUT_SECONDS * 1000, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_init(&wdt_config); esp_task_wdt_add(NULL);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, LED_COUNT); FastLED.clear(); FastLED.show();

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { display.clearDisplay(); display.display(); }

  WiFiManager wm; wm.setDebugOutput(false);
  if (!wm.autoConnect("SmartNotificationNode_AP")) { delay(3000); ESP.restart(); }

  MDNS.begin("smartnode");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  preferences.begin("notif-node", true);
  level = preferences.getString("level", "IDLE"); title = preferences.getString("title", "READY"); message = preferences.getString("message", "System Live");
  lastLevel = level; lastTitle = title; lastMessage = message;
  preferences.end();

  // روت‌های وب‌سرور بهینه
  server.on("/", HTTP_GET, handleRootDashboard);
  server.on("/notify", HTTP_POST, handleNotifyRoute);
  server.on("/api/action", HTTP_GET, handleActionAPI);
  server.begin();

  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);

  lastActionTime = millis();
  oledShow();
}

// ================= LOOP =================
void loop() {
  esp_task_wdt_reset();
  server.handleClient();

  if (!mqttClient.connected()) {
    reconnectMQTT();
  } else {
    mqttClient.loop();
  }

  checkButton();
  handleBuzzer();
  sendTelemetry();

  int currentInterval = (level == "CUSTOM") ? customBlinkSpeed : 70;
  if (millis() - lastAnimation > currentInterval) { lastAnimation = millis(); updateRGB(); }

  static unsigned long lastOledRefresh = 0;
  if (millis() - lastOledRefresh > 100) { lastOledRefresh = millis(); oledShow(); }
}
