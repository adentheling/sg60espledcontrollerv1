#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// CONFIG
#define MAX_STRIPS 4
#define MAX_LEDS 300

// AP MODE SETTINGS
const char* ap_ssid = "espled";
const char* ap_password = "ledcontroller";

// Static IP config for SoftAP
IPAddress local_ip(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// STATUS LED
#define STATUS_LED_PIN 48
Adafruit_NeoPixel statusLed(1, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// STRIP DEFINITION
struct StripInfo {
  Adafruit_NeoPixel strip;
  uint8_t pin;
  uint16_t count;
};

StripInfo strips[MAX_STRIPS] = {
  { Adafruit_NeoPixel(MAX_LEDS, 2, NEO_GRB + NEO_KHZ800), 2, 0 },
  { Adafruit_NeoPixel(MAX_LEDS, 3, NEO_GRB + NEO_KHZ800), 3, 0 }
};

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DynamicJsonDocument configDoc(4096);

bool manualMode = false;
uint8_t globalBrightness = 128;

void listSPIFFS() {
  Serial.println("Listing SPIFFS files:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.print(" - ");
    Serial.println(file.name());
    file = root.openNextFile();
  }
}

void loadConfig() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS mount failed!");
    return;
  }
  listSPIFFS();
  File f = SPIFFS.open("/config.json", "r");
  if (!f) {
    Serial.println("[WARN] config.json not found!");
    return;
  }
  DeserializationError e = deserializeJson(configDoc, f);
  if (e) {
    Serial.print("[ERROR] JSON parse error: ");
    Serial.println(e.c_str());
  } else {
    Serial.println("config.json loaded OK");
  }
  f.close();
}

void saveConfig() {
  File f = SPIFFS.open("/config.json", "w");
  if (!f) {
    Serial.println("[ERROR] config.json save fail!");
    return;
  }
  serializeJson(configDoc, f);
  f.close();
  Serial.println("config.json saved.");
}

void handleWsMessage(void* arg, uint8_t* data, size_t len) {
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    DynamicJsonDocument doc(512);
    auto error = deserializeJson(doc, (char*)data);
    if (error) {
      Serial.printf("[ERROR] websocket JSON parse: %s\n", error.c_str());
      return;
    }

    if (doc.containsKey("manual")) {
      manualMode = doc["manual"];
      Serial.printf("manualMode changed to: %d\n", manualMode);
    }
    if (doc.containsKey("led")) {
      uint16_t led = doc["led"];
      bool state = doc["state"];
      for (int s = 0; s < MAX_STRIPS; s++) {
        if (led < strips[s].count) {
          strips[s].strip.setPixelColor(led, state ? strips[s].strip.Color(255, 255, 255) : 0);
          strips[s].strip.show();
        }
      }
    }
    if (doc.containsKey("brightness")) {
      globalBrightness = doc["brightness"];
      Serial.printf("brightness set to %d\n", globalBrightness);
    }
  }
}

// New function to start AP with retries
void startAccessPoint() {
  const int maxRetries = 5;
  int attempt = 0;

  while (attempt < maxRetries) {
    Serial.printf("Attempting to start AP (try %d)...\n", attempt + 1);

    if (!WiFi.softAPConfig(local_ip, gateway, subnet)) {
      Serial.println("[ERROR] SoftAPConfig failed");
      delay(1000);
      attempt++;
      continue;
    }

    if (WiFi.softAP(ap_ssid, ap_password)) {
      Serial.println("AP started successfully.");
      statusLed.setPixelColor(0, statusLed.Color(0, 0, 20)); // blue
      statusLed.show();
      IPAddress ip = WiFi.softAPIP();
      Serial.printf("AP IP: %s\n", ip.toString().c_str());
      return;
    } else {
      Serial.println("[ERROR] AP failed to start!");
      statusLed.setPixelColor(0, statusLed.Color(20, 0, 0)); // red
      statusLed.show();
      delay(1000);
      attempt++;
    }
  }

  Serial.println("[ERROR] Failed to start AP after retries");
  statusLed.setPixelColor(0, statusLed.Color(20, 0, 0)); // red
  statusLed.show();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // onboard LED on
  Serial.begin(115200);
  delay(2000);
  Serial.println("HELLO FROM SETUP START");

  statusLed.begin();
  statusLed.setPixelColor(0, statusLed.Color(20, 0, 0)); // red = boot
  statusLed.show();

  loadConfig();

  delay(2000);  // give system a moment before starting AP

  startAccessPoint();

  // setup LED strips
  if (configDoc.containsKey("strips")) {
    JsonArray js = configDoc["strips"];
    int idx = 0;
    for (JsonObject s : js) {
      int pin = s["pin"];
      int count = s["ledCount"];
      strips[idx].pin = pin;
      strips[idx].count = count;
      strips[idx].strip.updateLength(count);
      strips[idx].strip.setPin(pin);
      strips[idx].strip.begin();
      strips[idx].strip.show();
      idx++;
    }
    manualMode = configDoc["manualMode"];
  } else {
    Serial.println("[WARN] config has no 'strips' array, skipping strips init");
  }

  // serve static site
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    String j;
    serializeJson(configDoc, j);
    req->send(200, "application/json", j);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (req->hasParam("plain", true)) {
      DynamicJsonDocument doc(4096);
      auto error = deserializeJson(doc, req->arg("plain"));
      if (error) {
        Serial.printf("[ERROR] JSON parse in /save: %s\n", error.c_str());
        req->send(400, "text/plain", "parse fail");
      } else {
        configDoc = doc;
        saveConfig();
        req->send(200, "text/plain", "saved");
      }
    }
  });

  server.onNotFound([](AsyncWebServerRequest* request) {
    Serial.printf("[INFO] redirecting %s to /\n", request->url().c_str());
    request->redirect("/");
  });

  ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.printf("[WS] Client %u connected\n", c->id());
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("[WS] Client %u disconnected\n", c->id());
    } else if (type == WS_EVT_DATA) {
      handleWsMessage(arg, data, len);
    }
  });
  server.addHandler(&ws);

  server.begin();
  Serial.println("HTTP server started");
}

// Watchdog in loop to check if AP is alive; restart if needed
unsigned long lastApCheck = 0;
const unsigned long apCheckInterval = 5000;  // check every 5 seconds

void loop() {
  // Check AP status periodically
  if (millis() - lastApCheck > apCheckInterval) {
    lastApCheck = millis();

    // AP should be running and have IP 192.168.4.1 at least
    IPAddress ip = WiFi.softAPIP();
    int stations = WiFi.softAPgetStationNum();

    if (ip != local_ip) {
      Serial.printf("[WATCHDOG] AP IP mismatch (got %s), restarting AP\n", ip.toString().c_str());
      startAccessPoint();
    } else if (stations < 0) {
      // unlikely but just in case
      Serial.println("[WATCHDOG] AP station number invalid, restarting AP");
      startAccessPoint();
    }
  }

  // LED status heartbeat
  if (WiFi.softAPgetStationNum() > 0) {
    statusLed.setPixelColor(0, statusLed.Color(0, 20, 0)); // green = client connected
  } else {
    statusLed.setPixelColor(0, statusLed.Color(0, 0, 20)); // blue = AP running
  }
  statusLed.show();

  // Your LED animation if not manualMode
  if (!manualMode) {
    static float phase = 0;
    phase += 0.002;
    if (phase > 1) phase = 0;
    uint16_t hue = phase * 65535;
    for (int s = 0; s < MAX_STRIPS; s++) {
      for (int i = 0; i < strips[s].count; i++) {
        uint32_t rawColor = strips[s].strip.gamma32(strips[s].strip.ColorHSV(hue));
        uint8_t r = (rawColor >> 16) & 0xFF;
        uint8_t g = (rawColor >> 8) & 0xFF;
        uint8_t b = rawColor & 0xFF;
        r = (r * globalBrightness) / 255;
        g = (g * globalBrightness) / 255;
        b = (b * globalBrightness) / 255;
        strips[s].strip.setPixelColor(i, strips[s].strip.Color(r, g, b));
      }
      strips[s].strip.show();
    }
  }
}
