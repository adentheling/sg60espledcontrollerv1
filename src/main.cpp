#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// CONFIG
#define MAX_STRIPS 4
#define MAX_LEDS 300

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

void loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  }
  File f = LittleFS.open("/config.json","r");
  if (!f) {
    Serial.println("config.json not found");
    return;
  }
  DeserializationError e = deserializeJson(configDoc,f);
  if (e) Serial.println("JSON parse error");
  else Serial.println("config loaded");
  f.close();
}

void saveConfig() {
  File f = LittleFS.open("/config.json","w");
  if (!f) {
    Serial.println("config.json save fail");
    return;
  }
  serializeJson(configDoc,f);
  f.close();
  Serial.println("config saved");
}

void handleWsMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT){
    DynamicJsonDocument doc(512);
    deserializeJson(doc, (char*)data);
    if(doc.containsKey("manual")){
      manualMode = doc["manual"];
      Serial.printf("manual mode: %d\n", manualMode);
    }
    if(doc.containsKey("led")){
      uint16_t led = doc["led"];
      bool state = doc["state"];
      for(int s=0;s<MAX_STRIPS;s++){
        if(led < strips[s].count){
          if(state) strips[s].strip.setPixelColor(led, strips[s].strip.Color(255,255,255));
          else strips[s].strip.setPixelColor(led, 0);
          strips[s].strip.show();
        }
      }
    }
  }
}

void setup(){
  Serial.begin(115200);
  if (!LittleFS.begin()) Serial.println("LittleFS problem");
  loadConfig();

  JsonArray js = configDoc["strips"];
  int idx=0;
  for(JsonObject s : js){
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

  server.serveStatic("/",LittleFS,"/").setDefaultFile("index.html");
  server.on("/config.json",HTTP_GET,[](AsyncWebServerRequest *req){
    String j;
    serializeJson(configDoc,j);
    req->send(200,"application/json",j);
  });
  server.on("/save",HTTP_POST,[](AsyncWebServerRequest *req){
    if(req->hasParam("plain",true)){
      DynamicJsonDocument doc(4096);
      deserializeJson(doc, req->arg("plain"));
      configDoc = doc;
      saveConfig();
      req->send(200,"text/plain","saved");
    }
  });
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type,void *arg,uint8_t *data,size_t len){
    if(type==WS_EVT_DATA) handleWsMessage(arg,data,len);
  });
  server.addHandler(&ws);
  server.begin();
}

void loop(){
  if(!manualMode){
    static float phase = 0;
    phase += 0.002;
    if(phase>1)phase=0;
    uint16_t hue = phase*65535;
    for(int s=0;s<MAX_STRIPS;s++){
      for(int i=0;i<strips[s].count;i++){
        strips[s].strip.setPixelColor(i, strips[s].strip.gamma32(strips[s].strip.ColorHSV(hue)));
      }
      strips[s].strip.show();
    }
  }
}
