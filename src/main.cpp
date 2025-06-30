#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

AsyncWebServer server(80);

// For maximum 2 strips initially
Adafruit_NeoPixel strip1(60, 2, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(30, 3, NEO_GRB + NEO_KHZ800);

// JSON config
DynamicJsonDocument configDoc(2048);

// Current settings
bool manualMode = false;

void loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed!");
    return;
  }
  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    Serial.println("config.json not found");
    return;
  }
  DeserializationError err = deserializeJson(configDoc, f);
  if (err) {
    Serial.println("JSON parse failed");
  } else {
    Serial.println("Config loaded:");
    serializeJsonPretty(configDoc, Serial);
  }
  f.close();
}

void setup() {
  Serial.begin(115200);

  loadConfig();

  // read from JSON
  JsonArray strips = configDoc["strips"];
  for (JsonObject s : strips) {
    int pin = s["pin"];
    int count = s["ledCount"];
    if (pin == 2) {
      strip1.updateLength(count);
      strip1.setPin(pin);
      strip1.begin();
      strip1.show();
    }
    if (pin == 3) {
      strip2.updateLength(count);
      strip2.setPin(pin);
      strip2.begin();
      strip2.show();
    }
  }

  manualMode = configDoc["manualMode"];

  // Serve static web
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();
}

void loop() {
  // later: manual mode vs default mode animation
}
