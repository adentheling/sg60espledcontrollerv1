#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <algorithm>    // for std::min
#include <esp_wifi.h>   // for esp_wifi_set_max_tx_power

#define MAX_STRIPS 4
#define MAX_LEDS_PER_STRIP 150
#define MAX_GROUPS_PER_STRIP 8
#define MAX_GROUP_SIZE 50

struct LEDStrip {
  uint8_t pin;
  uint16_t numLeds;
  Adafruit_NeoPixel* strip;
  uint8_t brightness;
  float speed;
  float phase;

  bool ledStates[MAX_LEDS_PER_STRIP];      // ON/OFF per LED
  uint8_t groupCount;
  uint8_t groupSizes[MAX_GROUPS_PER_STRIP];
  uint8_t groups[MAX_GROUPS_PER_STRIP][MAX_GROUP_SIZE];  // LED indices per group
};

LEDStrip strips[MAX_STRIPS];
uint8_t stripCount = 0;

AsyncWebServer server(80);

const char* ssid = "espled";
const char* password = "legolego";

void setupWiFi() {
  WiFi.softAP(ssid, password);
  Serial.println("\nWiFi started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Set Wi-Fi output power to 8.5 dBm (units = 0.25 dBm steps, so 8.5/0.25 = 34)
  esp_wifi_set_max_tx_power(34);
  Serial.println("WiFi max TX power set to 8.5 dBm");
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
  s.speed = 0.005f;
  s.phase = 0.0f;
  s.groupCount = 0;
  for (int i = 0; i < numLeds; i++) s.ledStates[i] = true;
  stripCount++;
  return true;
}

void saveConfigToFile() {
  DynamicJsonDocument doc(16384);

  JsonArray stripsArr = doc.createNestedArray("strips");
  for (int i = 0; i < stripCount; i++) {
    LEDStrip &s = strips[i];
    JsonObject stripObj = stripsArr.createNestedObject();

    stripObj["pin"] = s.pin;
    stripObj["numLeds"] = s.numLeds;
    stripObj["brightness"] = s.brightness;
    stripObj["speed"] = s.speed;
    stripObj["groupCount"] = s.groupCount;

    JsonArray ledStatesArr = stripObj.createNestedArray("ledStates");
    for (int j = 0; j < s.numLeds; j++) {
      ledStatesArr.add(s.ledStates[j]);
    }

    JsonArray groupsArr = stripObj.createNestedArray("groups");
    for (int g = 0; g < s.groupCount; g++) {
      JsonObject groupObj = groupsArr.createNestedObject();
      groupObj["size"] = s.groupSizes[g];
      JsonArray ledsArr = groupObj.createNestedArray("leds");
      for (int k = 0; k < s.groupSizes[g]; k++) {
        ledsArr.add(s.groups[g][k]);
      }
    }
  }

  File file = SPIFFS.open("/config.json", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open config.json for writing");
    return;
  }
  serializeJson(doc, file);
  file.close();
  Serial.println("Config saved to /config.json");
}

void loadConfigFromFile() {
  if (!SPIFFS.exists("/config.json")) {
    Serial.println("No config.json found, skipping load");
    return;
  }
  File file = SPIFFS.open("/config.json", FILE_READ);
  if (!file) {
    Serial.println("Failed to open config.json for reading");
    return;
  }

  size_t size = file.size();
  if (size > 16384) {
    Serial.println("Config file too large");
    file.close();
    return;
  }

  std::unique_ptr<char[]> buf(new char[size + 1]);
  file.readBytes(buf.get(), size);
  buf[size] = 0;
  file.close();

  DynamicJsonDocument doc(16384);
  auto err = deserializeJson(doc, buf.get());
  if (err) {
    Serial.println("Failed to parse config.json");
    return;
  }

  JsonArray stripsArr = doc["strips"].as<JsonArray>();
  stripCount = 0;
  for (JsonObject stripObj : stripsArr) {
    uint8_t pin = stripObj["pin"];
    uint16_t numLeds = stripObj["numLeds"];

    if (!addStrip(pin, numLeds)) {
      Serial.printf("Failed to add strip for pin %d\n", pin);
      continue;
    }

    LEDStrip &s = strips[stripCount - 1];
    s.brightness = stripObj["brightness"] | 50;
    s.speed = stripObj["speed"] | 0.005f;
    s.groupCount = stripObj["groupCount"] | 0;

    JsonArray ledStatesArr = stripObj["ledStates"].as<JsonArray>();
    size_t countStates = std::min(ledStatesArr.size(), static_cast<size_t>(s.numLeds));
    for (size_t i = 0; i < countStates; i++) {
      s.ledStates[i] = ledStatesArr[i];
    }
    for (size_t i = countStates; i < s.numLeds; i++) {
      s.ledStates[i] = true; // default on
    }

    JsonArray groupsArr = stripObj["groups"].as<JsonArray>();
    for (int g = 0; g < s.groupCount && g < groupsArr.size(); g++) {
      JsonObject groupObj = groupsArr[g];
      s.groupSizes[g] = groupObj["size"] | 0;
      JsonArray ledsArr = groupObj["leds"].as<JsonArray>();
      for (int k = 0; k < s.groupSizes[g] && k < ledsArr.size(); k++) {
        s.groups[g][k] = ledsArr[k];
      }
    }
  }
  Serial.println("Config loaded from /config.json");
}

// Web Handlers

void handleRoot(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/index.html", "text/html");
}

void handleState(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(16384);
  JsonArray stripsArr = doc.createNestedArray("strips");
  for (int i = 0; i < stripCount; i++) {
    LEDStrip &s = strips[i];
    JsonObject stripObj = stripsArr.createNestedObject();

    stripObj["id"] = i;
    stripObj["pin"] = s.pin;
    stripObj["numLeds"] = s.numLeds;
    stripObj["brightness"] = s.brightness;
    stripObj["speed"] = s.speed;
    stripObj["groupCount"] = s.groupCount;

    JsonArray ledStatesArr = stripObj.createNestedArray("ledStates");
    for (int j = 0; j < s.numLeds; j++) {
      ledStatesArr.add(s.ledStates[j]);
    }

    JsonArray groupsArr = stripObj.createNestedArray("groups");
    for (int g = 0; g < s.groupCount; g++) {
      JsonObject groupObj = groupsArr.createNestedObject();
      groupObj["size"] = s.groupSizes[g];
      JsonArray ledsArr = groupObj.createNestedArray("leds");
      for (int k = 0; k < s.groupSizes[g]; k++) {
        ledsArr.add(s.groups[g][k]);
      }
    }
  }

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

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
  saveConfigToFile();
  request->send(200, "text/plain", strips[stripId].ledStates[led] ? "ON" : "OFF");
}

void handleSetBrightness(AsyncWebServerRequest *request) {
  if (!request->hasParam("strip") || !request->hasParam("brightness")) {
    request->send(400, "text/plain", "Missing parameters");
    return;
  }
  int stripId = request->getParam("strip")->value().toInt();
  int b = request->getParam("brightness")->value().toInt();
  if (stripId < 0 || stripId >= stripCount || b < 0 || b > 255) {
    request->send(400, "text/plain", "Invalid brightness or strip");
    return;
  }
  strips[stripId].brightness = b;
  saveConfigToFile();
  request->send(200, "text/plain", "OK");
}

void handleSetSpeed(AsyncWebServerRequest *request) {
  if (!request->hasParam("strip") || !request->hasParam("speed")) {
    request->send(400, "text/plain", "Missing parameters");
    return;
  }
  int stripId = request->getParam("strip")->value().toInt();
  float spd = request->getParam("speed")->value().toFloat();
  if (stripId < 0 || stripId >= stripCount || spd < 0 || spd > 0.5f) {
    request->send(400, "text/plain", "Invalid speed or strip");
    return;
  }
  strips[stripId].speed = spd;
  saveConfigToFile();
  request->send(200, "text/plain", "OK");
}

void handleAddStrip(AsyncWebServerRequest *request) {
  if (!request->hasParam("pin") || !request->hasParam("numLeds")) {
    request->send(400, "text/plain", "Missing parameters");
    return;
  }
  int pin = request->getParam("pin")->value().toInt();
  int numLeds = request->getParam("numLeds")->value().toInt();
  if (numLeds <= 0 || numLeds > MAX_LEDS_PER_STRIP || stripCount >= MAX_STRIPS) {
    request->send(400, "text/plain", "Invalid parameters or max strips reached");
    return;
  }
  if (addStrip(pin, numLeds)) {
    saveConfigToFile();
    request->send(200, "text/plain", "Strip added");
  } else {
    request->send(500, "text/plain", "Failed to add strip");
  }
}

void handleRemoveStrip(AsyncWebServerRequest *request) {
  if (!request->hasParam("strip")) {
    request->send(400, "text/plain", "Missing parameter");
    return;
  }
  int stripId = request->getParam("strip")->value().toInt();
  if (stripId < 0 || stripId >= stripCount) {
    request->send(400, "text/plain", "Invalid strip");
    return;
  }

  delete strips[stripId].strip;

  // Shift remaining strips down
  for (int i = stripId; i < stripCount - 1; i++) {
    strips[i] = strips[i + 1];
  }
  stripCount--;
  saveConfigToFile();
  request->send(200, "text/plain", "Strip removed");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    while (1);
  }

  setupWiFi();

  loadConfigFromFile();

  // If no strips, create a default strip for demo
  if (stripCount == 0) {
    addStrip(2, 60);
    saveConfigToFile();
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/state", HTTP_GET, handleState);
  server.on("/toggle", HTTP_GET, handleToggleLED);
  server.on("/brightness", HTTP_GET, handleSetBrightness);
  server.on("/speed", HTTP_GET, handleSetSpeed);
  server.on("/addstrip", HTTP_GET, handleAddStrip);
  server.on("/removestrip", HTTP_GET, handleRemoveStrip);
  server.serveStatic("/", SPIFFS, "/");

  server.begin();
  Serial.println("Server started");
}

void loop() {
  for (int s = 0; s < stripCount; s++) {
    LEDStrip &strip = strips[s];
    strip.phase += strip.speed;
    if (strip.phase > 1.0f) strip.phase -= 1.0f;

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
  delay(20);
}
