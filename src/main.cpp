#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <esp_wifi.h>

// =================== CONFIG ===================
#define STATUS_LED_PIN 48
#define LED_STRIP_1_PIN 3
#define LED_STRIP_2_PIN 10

#define MAX_LEDS_PER_STRIP 200
#define JSON_BUFFER_SIZE 16384

// =================== STRUCTS ===================
struct StripConfig {
  uint16_t numLeds;
  uint8_t brightness;
  float speed;
  uint8_t mode;
  bool ledStates[MAX_LEDS_PER_STRIP];
};

struct GlobalConfig {
  StripConfig strips[2];
  bool randomize;
  uint16_t randomizeInterval;
  bool randomizeModes[4]; // up to 4 modes
  uint8_t statusLedBrightness;
  bool manualMode;
  uint8_t selectedMode;
};

GlobalConfig config;

Adafruit_NeoPixel* strips[2];
AsyncWebServer server(80);

const char* ssid = "espleds";
const char* password = "legoledcontroller";

// =================== STATUS LED ===================
void setStatusLED(uint8_t r, uint8_t g, uint8_t b) {
  // control status LED brightness using PWM on channel 0
  const int statusLedChannel = 0;
  ledcAttachPin(STATUS_LED_PIN, statusLedChannel);
  ledcSetup(statusLedChannel, 5000, 8);
  uint8_t v = max(max(r, g), b);
  v = (v * config.statusLedBrightness) / 255;
  ledcWrite(statusLedChannel, v);
}

// =================== JSON SAVE/LOAD ===================
void saveConfig() {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  for (int i = 0; i < 2; i++) {
    doc["strips"][i]["numLeds"] = config.strips[i].numLeds;
    doc["strips"][i]["brightness"] = config.strips[i].brightness;
    doc["strips"][i]["speed"] = config.strips[i].speed;
    doc["strips"][i]["mode"] = config.strips[i].mode;
    JsonArray states = doc["strips"][i].createNestedArray("ledStates");
    for (int j = 0; j < config.strips[i].numLeds; j++)
      states.add(config.strips[i].ledStates[j]);
  }
  doc["randomize"] = config.randomize;
  doc["randomizeInterval"] = config.randomizeInterval;
  JsonArray modes = doc.createNestedArray("randomizeModes");
  for (int i = 0; i < 4; i++)
    modes.add(config.randomizeModes[i]);
  doc["statusLedBrightness"] = config.statusLedBrightness;
  doc["manualMode"] = config.manualMode;
  doc["selectedMode"] = config.selectedMode;

  File file = SPIFFS.open("/config.json", "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.println("Config saved.");
  }
}

void loadConfig() {
  if (!SPIFFS.exists("/config.json")) {
    Serial.println("No config.json, using defaults");
    return;
  }
  File file = SPIFFS.open("/config.json");
  if (file) {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError e = deserializeJson(doc, file);
    if (!e) {
      for (int i = 0; i < 2; i++) {
        config.strips[i].numLeds = doc["strips"][i]["numLeds"] | 60;
        config.strips[i].brightness = doc["strips"][i]["brightness"] | 100;
        config.strips[i].speed = doc["strips"][i]["speed"] | 0.01;
        config.strips[i].mode = doc["strips"][i]["mode"] | 0;
        JsonArray states = doc["strips"][i]["ledStates"];
        for (int j = 0; j < states.size() && j < MAX_LEDS_PER_STRIP; j++)
          config.strips[i].ledStates[j] = states[j];
      }
      config.randomize = doc["randomize"] | false;
      config.randomizeInterval = doc["randomizeInterval"] | 5;
      JsonArray modes = doc["randomizeModes"];
      for (int i = 0; i < 4; i++)
        config.randomizeModes[i] = modes[i] | true;
      config.statusLedBrightness = doc["statusLedBrightness"] | 255;
      config.manualMode = doc["manualMode"] | false;
      config.selectedMode = doc["selectedMode"] | 0;
    }
    file.close();
  }
}

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  SPIFFS.begin(true);

  // status LED setup
  ledcSetup(0, 5000, 8);
  ledcAttachPin(STATUS_LED_PIN, 0);
  ledcWrite(0, 0);
  setStatusLED(255, 0, 0); // boot = red

  // defaults
  config.strips[0].mode = 0;
  config.strips[1].mode = 0;

  // WiFi
  IPAddress local_IP(192, 168, 4, 10);
  IPAddress gateway(192, 168, 4, 10);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);
  esp_wifi_set_max_tx_power(34);

  setStatusLED(0, 0, 255); // blue = wifi up

  loadConfig();

  strips[0] = new Adafruit_NeoPixel(config.strips[0].numLeds, LED_STRIP_1_PIN, NEO_GRB + NEO_KHZ800);
  strips[1] = new Adafruit_NeoPixel(config.strips[1].numLeds, LED_STRIP_2_PIN, NEO_GRB + NEO_KHZ800);
  strips[0]->begin();
  strips[1]->begin();
  strips[0]->show();
  strips[1]->show();

  // serve index
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });

  // JSON update handler
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument doc(JSON_BUFFER_SIZE);
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        JsonObject json = doc.as<JsonObject>();
        config.statusLedBrightness = json["statusLedBrightness"] | config.statusLedBrightness;
        config.strips[0].numLeds = json["numLedsPin3"] | config.strips[0].numLeds;
        config.strips[1].numLeds = json["numLedsPin10"] | config.strips[1].numLeds;
        config.manualMode = json["manualMode"] | config.manualMode;
        config.randomize = json["randomMode"] | config.randomize;
        config.randomizeInterval = json["randomizeInterval"] | config.randomizeInterval;
        config.selectedMode = json["selectedMode"] | config.selectedMode;

        saveConfig();
        request->send(200, "text/plain", "OK");
      } else {
        request->send(400, "text/plain", "Invalid JSON");
      }
    }
  );


  server.begin();
  Serial.println("Server started at 192.168.4.10");
}

uint32_t frameCount = 0;

void loop() {
  frameCount++;
  static bool blinkState = false;

  int clients = WiFi.softAPgetStationNum();
  if (clients == 0) {
    setStatusLED(0, 0, 255); // blue
  } else if (clients == 1) {
    setStatusLED(0, 255, 0); // green
  } else if (clients > 1) {
    if (frameCount % 50 == 0) blinkState = !blinkState;
    setStatusLED(0, blinkState ? 255 : 0, 0);
  }

  for (int s = 0; s < 2; s++) {
    auto &strip = strips[s];
    auto &conf = config.strips[s];

    switch (conf.mode) {
      case 0: { // RGB cycle
        static float phase = 0;
        phase += conf.speed;
        if (phase > 1.0) phase -= 1.0;
        uint16_t hue = phase * 65535;
        uint32_t color = Adafruit_NeoPixel::ColorHSV(hue, 255, 255);
        for (int i = 0; i < conf.numLeds; i++)
          strip->setPixelColor(i, color);
        strip->setBrightness(conf.brightness);
        strip->show();
        break;
      }
      // you can expand other patterns here
    }
  }

  delay(20);
}
