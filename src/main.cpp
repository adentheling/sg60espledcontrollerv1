#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Preferences.h>

#define MAX_STRIPS 4
#define MAX_LEDS_PER_STRIP 150
#define MAX_GROUPS_PER_STRIP 10
#define MAX_GROUP_SIZE 50

struct LEDStrip {
  uint8_t pin;
  uint16_t numLeds;
  Adafruit_NeoPixel* strip;
  uint8_t brightness;
  float speed;
  float phase;
  bool ledStates[MAX_LEDS_PER_STRIP];
  uint8_t groups[MAX_GROUPS_PER_STRIP][MAX_GROUP_SIZE];
  uint8_t groupSizes[MAX_GROUPS_PER_STRIP];
  uint8_t groupCount;
};

LEDStrip strips[MAX_STRIPS];
uint8_t stripCount = 0;

AsyncWebServer server(80);
Preferences preferences;

const char* ssid = "ESP32_LED_Controller";
const char* password = "12345678";

void setupWiFi() {
  WiFi.softAP(ssid, password);
  Serial.println("\nWiFi started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
}

bool addStrip(uint8_t pin, uint16_t numLeds) {
  if (stripCount >= MAX_STRIPS) return false;
  LEDStrip &s = strips[stripCount];
  s.pin = pin;
  s.numLeds = numLeds;
  s.strip = new Adafruit_NeoPixel(numLeds, pin, NEO_GRB + NEO_KHZ800);
  s.strip->begin();
  s.strip->show();
  s.brightness = 50;
  s.speed = 0.005;
  s.phase = 0;
  s.groupCount = 0;
  for (int i = 0; i < numLeds; i++) s.ledStates[i] = true;
  stripCount++;
  return true;
}

void saveConfig() {
  preferences.begin("ledcfg", false);
  preferences.putUChar("stripCount", stripCount);
  for (int i = 0; i < stripCount; i++) {
    String baseKey = "s" + String(i);
    preferences.putUChar((baseKey + "p").c_str(), strips[i].pin);
    preferences.putUShort((baseKey + "n").c_str(), strips[i].numLeds);
    preferences.putUChar((baseKey + "b").c_str(), strips[i].brightness);
    preferences.putFloat((baseKey + "sp").c_str(), strips[i].speed);
    preferences.putUChar((baseKey + "gc").c_str(), strips[i].groupCount);
    preferences.putBytes((baseKey + "leds").c_str(), strips[i].ledStates, strips[i].numLeds);
    for (int g = 0; g < strips[i].groupCount; g++) {
      preferences.putUChar((baseKey + "gs" + String(g)).c_str(), strips[i].groupSizes[g]);
      preferences.putBytes((baseKey + "g" + String(g)).c_str(), strips[i].groups[g], strips[i].groupSizes[g]);
    }
  }
  preferences.end();
  Serial.println("Config saved");
}

void loadConfig() {
  preferences.begin("ledcfg", true);
  stripCount = preferences.getUChar("stripCount", 0);
  if (stripCount == 0) {
    preferences.end();
    Serial.println("No saved config");
    return;
  }
  for (int i = 0; i < stripCount; i++) {
    String baseKey = "s" + String(i);
    uint8_t pin = preferences.getUChar((baseKey + "p").c_str(), 0);
    uint16_t numLeds = preferences.getUShort((baseKey + "n").c_str(), 0);
    addStrip(pin, numLeds);  // initializes strip and ledStates = true

    strips[i].brightness = preferences.getUChar((baseKey + "b").c_str(), 50);
    strips[i].speed = preferences.getFloat((baseKey + "sp").c_str(), 0.005);
    strips[i].groupCount = preferences.getUChar((baseKey + "gc").c_str(), 0);

    preferences.getBytes((baseKey + "leds").c_str(), strips[i].ledStates, strips[i].numLeds);
    for (int g = 0; g < strips[i].groupCount; g++) {
      strips[i].groupSizes[g] = preferences.getUChar((baseKey + "gs" + String(g)).c_str(), 0);
      preferences.getBytes((baseKey + "g" + String(g)).c_str(), strips[i].groups[g], strips[i].groupSizes[g]);
    }
  }
  preferences.end();
  Serial.println("Config loaded");
}

// Handlers from previous code here...
// Add saveConfig() calls after any config change (e.g., toggle LED, set brightness/speed, add group)

void handleToggleLED(AsyncWebServerRequest *request) {
  if (!request->hasParam("strip") || !request->hasParam("led")) {
    request->send(400, "text/plain", "Missing parameters");
    return;
  }
  int stripId = request->getParam("strip")->value().toInt();
  int led = request->getParam("led")->value().toInt();
  if (stripId < 0 || stripId >= stripCount || led < 0 || led >= strips[stripId].numLeds) {
    request->send(400, "text/plain", "Invalid strip or LED index");
    return;
  }
  strips[stripId].ledStates[led] = !strips[stripId].ledStates[led];
  saveConfig();
  request->send(200, "text/plain", strips[stripId].ledStates[led] ? "ON" : "OFF");
}

void handleSetBrightness(AsyncWebServerRequest *request) {
  if (!request->hasParam("strip") || !request->hasParam("b")) {
    request->send(400, "text/plain", "Missing parameters");
    return;
  }
  int stripId = request->getParam("strip")->value().toInt();
  int b = request->getParam("b")->value().toInt();
  if (stripId >= 0 && stripId < stripCount) {
    strips[stripId].brightness = b;
    saveConfig();
    request->send(200, "text/plain", "OK");
  } else {
    request->send(400, "text/plain", "Invalid strip");
  }
}

void handleSetSpeed(AsyncWebServerRequest *request) {
  if (!request->hasParam("strip") || !request->hasParam("s")) {
    request->send(400, "text/plain", "Missing parameters");
    return;
  }
  int stripId = request->getParam("strip")->value().toInt();
  float s = request->getParam("s")->value().toFloat();
  if (stripId >= 0 && stripId < stripCount) {
    strips[stripId].speed = s;
    saveConfig();
    request->send(200, "text/plain", "OK");
  } else {
    request->send(400, "text/plain", "Invalid strip");
  }
}

void handleAddGroup(AsyncWebServerRequest *request) {
  if (!request->hasParam("strip") || !request->hasParam("leds")) {
    request->send(400, "text/plain", "Missing parameters");
    return;
  }
  int stripId = request->getParam("strip")->value().toInt();
  String ledList = request->getParam("leds")->value();

  if (stripId < 0 || stripId >= stripCount || strips[stripId].groupCount >= MAX_GROUPS_PER_STRIP) {
    request->send(400, "text/plain", "Invalid strip or max groups reached");
    return;
  }

  int groupId = strips[stripId].groupCount;
  strips[stripId].groupSizes[groupId] = 0;
  int idx = 0;
  ledList.replace(" ", "");
  while (ledList.length() > 0 && idx < MAX_GROUP_SIZE) {
    int comma = ledList.indexOf(',');
    String token = (comma >= 0) ? ledList.substring(0, comma) : ledList;
    int dash = token.indexOf('-');
    if (dash >= 0) {
      int start = token.substring(0, dash).toInt();
      int end = token.substring(dash + 1).toInt();
      for (int i = start; i <= end && idx < MAX_GROUP_SIZE; i++) {
        if (i < strips[stripId].numLeds) {
          strips[stripId].groups[groupId][idx++] = i;
        }
      }
    } else {
      int val = token.toInt();
      if (val < strips[stripId].numLeds) {
        strips[stripId].groups[groupId][idx++] = val;
      }
    }
    if (comma >= 0) ledList = ledList.substring(comma + 1);
    else break;
  }
  strips[stripId].groupSizes[groupId] = idx;
  strips[stripId].groupCount++;
  saveConfig();
  request->send(200, "text/plain", "Group added");
}

void setup() {
  Serial.begin(115200);
  SPIFFS.begin(true);
  setupWiFi();

  loadConfig();  // load saved config, or nothing if first time

  // If no saved config, add example strips:
  if (stripCount == 0) {
    addStrip(2, 60);
    addStrip(3, 30);
    saveConfig();
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });
  server.on("/state", HTTP_GET, handleState);
  server.on("/toggle", HTTP_GET, handleToggleLED);
  server.on("/setBrightness", HTTP_GET, handleSetBrightness);
  server.on("/setSpeed", HTTP_GET, handleSetSpeed);
  server.on("/addGroup", HTTP_GET, handleAddGroup);
  server.serveStatic("/", SPIFFS, "/");

  server.begin();
}

void loop() {
  for (int s = 0; s < stripCount; s++) {
    LEDStrip &strip = strips[s];
    strip.phase += strip.speed;
    if (strip.phase > 1.0) strip.phase -= 1.0;

    uint16_t hue = strip.phase * 65535;
    uint32_t color = Adafruit_NeoPixel::ColorHSV(hue, 255, 255);
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    r = (r * strip.brightness) / 255;
    g = (g * strip.brightness) / 255;
    b = (b * strip.brightness) / 255;

    for (int i = 0; i < strip.numLeds; i++) {
      if (strip.ledStates[i]) {
        strip.strip->setPixelColor(i, r, g, b);
      } else {
        strip.strip->setPixelColor(i, 0);
      }
    }
    strip.strip->show();
  }
  delay(10);
}
