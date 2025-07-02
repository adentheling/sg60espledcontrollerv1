#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

// CONFIG
#define STATUS_LED_PIN 48
#define LED_STRIP_1_PIN 3
#define LED_STRIP_2_PIN 10
#define MAX_LEDS_PER_STRIP 200
#define JSON_BUFFER_SIZE 8192

// WIFI
const char* ssid = "espleds";
const char* password = "legoledcontroller";

// Global
Adafruit_NeoPixel* statusLed;
Adafruit_NeoPixel* strips[2];
AsyncWebServer server(80);

// Config struct
struct StripConfig {
  uint16_t numLeds = 60;
  uint8_t brightness = 100;
  float speed = 0.01;
  uint8_t mode = 0;
};

struct GlobalConfig {
  StripConfig strips[2];
  uint8_t statusLedBrightness = 255;
  bool manualMode = false;
} config;

volatile bool gotRequest = false;

void setStatusLED(uint8_t r, uint8_t g, uint8_t b) {
  if (statusLed) {
    uint8_t scaled_r = (r * config.statusLedBrightness) / 255;
    uint8_t scaled_g = (g * config.statusLedBrightness) / 255;
    uint8_t scaled_b = (b * config.statusLedBrightness) / 255;
    statusLed->setPixelColor(0, scaled_r, scaled_g, scaled_b);
    statusLed->show();
  }
}

void initStrips() {
  for (int i=0;i<2;i++) {
    if (strips[i]) delete strips[i];
    strips[i] = new Adafruit_NeoPixel(config.strips[i].numLeds, i==0?LED_STRIP_1_PIN:LED_STRIP_2_PIN, NEO_GRB + NEO_KHZ800);
    strips[i]->begin();
    strips[i]->setBrightness(config.strips[i].brightness);
    strips[i]->show();
  }
}

void setup() {
  Serial.begin(115200);

  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS fail");
    while(1);
  }

  statusLed = new Adafruit_NeoPixel(1, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
  statusLed->begin();
  statusLed->show();
  setStatusLED(255,0,0); // boot red

  initStrips();

  // WiFi
  IPAddress ip(192,168,4,10);
  WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0));
  WiFi.softAP(ssid,password);
  esp_wifi_set_max_tx_power(34);
  setStatusLED(0,0,255); // blue wifi up

  // serve index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    File file = SPIFFS.open("/index.html");
    if (!file) {
      setStatusLED(255,0,255); // pink = missing
      request->send(404,"text/plain","index.html missing");
    } else {
      setStatusLED(255,255,0); // yellow = file found
      request->send(file,"text/html");
    }
    gotRequest=true;
  });

  // config
  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    doc["statusLedBrightness"] = config.statusLedBrightness;
    doc["numLeds1"] = config.strips[0].numLeds;
    doc["numLeds2"] = config.strips[1].numLeds;
    request->send(200,"application/json",String("{\"ok\":true}"));
    gotRequest=true;
  });

  server.begin();
  Serial.println("Server started at 192.168.4.10");
}

uint32_t frameCount=0;
uint32_t lastRequestCheck=0;

void loop() {
  frameCount++;

  int clients = WiFi.softAPgetStationNum();
  static bool blinkState=false;

  if (clients==0) {
    setStatusLED(0,0,255); // blue
  } else if (clients==1) {
    setStatusLED(0,255,0); // green
  } else {
    if(frameCount%50==0) blinkState=!blinkState;
    setStatusLED(255, blinkState?255:0,0); // blink red
  }

  if(millis()-lastRequestCheck>5000){
    if(!gotRequest){
      static bool fastBlink=false;
      fastBlink=!fastBlink;
      setStatusLED(fastBlink?255:0,0,0); // fast red
    }
    gotRequest=false;
    lastRequestCheck=millis();
  }

  // run default RGB cycle
  for(int s=0;s<2;s++){
    static float phase[2]={0,0};
    phase[s]+=config.strips[s].speed;
    if(phase[s]>1.0) phase[s]-=1.0;
    uint16_t hue = phase[s]*65535;
    uint32_t c = Adafruit_NeoPixel::ColorHSV(hue,255,255);
    for(int i=0;i<config.strips[s].numLeds;i++)
      strips[s]->setPixelColor(i,c);
    strips[s]->show();
  }

  delay(20);
}
