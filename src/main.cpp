#include <Arduino.h>
#define FASTLED_INTERNAL
#include <FastLED.h>

#include <DNSServer.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include <Preferences.h>

#include <ArduinoOTA.h>

#include "Globals.h"
#include "Canvas/Canvas.h"
#include "Kasa/KasaManager.h"
#include "Claude/ClaudeStatus.h"
#include "index"
#include "credentials.h"


const char* WIFI_SSID = CONFIG_WIFI_SSID;
const char* WIFI_PASSWORD = CONFIG_WIFI_PASSWORD;

// Preset storage
Preferences preferences;
#define NUM_PRESETS 5

struct Preset {
  uint8_t r, g, b;
  uint8_t brightness;
  uint8_t animation;
  bool animating;
};

Preset presets[NUM_PRESETS];
CRGB currentColor = CRGB::Red;  // Track current color
uint8_t currentAnimation = 1;   // Track current animation type


//set port
AsyncWebServer server(80);
//http request
String header;

//making LED data
CRGB leds[NUM_LEDS] = {0};
//static int canvasArr[NUM_ROWS][NUM_ROWS];

//create canvas
Canvas canvas{};

// Kasa device manager
KasaManager kasaManager;

// Claude session status manager
ClaudeStatus claudeStatus;

// areas to draw on canvas
Rectangle rightHalf{0, 0, ROW_LENGTH/2, NUM_ROWS}; 
Rectangle leftHalf{ROW_LENGTH/2, 0, ROW_LENGTH/2, NUM_ROWS}; 
Rectangle topHalf{0, 0, ROW_LENGTH, NUM_ROWS/2};
Rectangle bottomHalf{0, NUM_ROWS/2, ROW_LENGTH, NUM_ROWS/2};
Rectangle fullCanvas{0, 0, ROW_LENGTH, NUM_ROWS};


//testing, NOT MY CODE
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

// Load presets from flash
void loadPresets() {
  preferences.begin("presets", true);  // read-only
  for (int i = 0; i < NUM_PRESETS; i++) {
    String key = "p" + String(i);
    presets[i].r = preferences.getUChar((key + "r").c_str(), 255);
    presets[i].g = preferences.getUChar((key + "g").c_str(), 0);
    presets[i].b = preferences.getUChar((key + "b").c_str(), 0);
    presets[i].brightness = preferences.getUChar((key + "br").c_str(), 50);
    presets[i].animation = preferences.getUChar((key + "an").c_str(), 1);
    presets[i].animating = preferences.getBool((key + "on").c_str(), false);
  }
  preferences.end();
}

// Save a single preset to flash
void savePreset(int index) {
  if (index < 0 || index >= NUM_PRESETS) return;
  preferences.begin("presets", false);  // read-write
  String key = "p" + String(index);
  preferences.putUChar((key + "r").c_str(), presets[index].r);
  preferences.putUChar((key + "g").c_str(), presets[index].g);
  preferences.putUChar((key + "b").c_str(), presets[index].b);
  preferences.putUChar((key + "br").c_str(), presets[index].brightness);
  preferences.putUChar((key + "an").c_str(), presets[index].animation);
  preferences.putBool((key + "on").c_str(), presets[index].animating);
  preferences.end();
}

void setup()
{
  //connect to serial monitor
  Serial.begin(9600);
  Serial.println("Connected");

  // Load saved presets
  loadPresets();

  //add LED and data pin to FastLED
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  // build canvas
  canvas.buildCanvas(NUM_ROWS, ROW_LENGTH);

  // load default canvas state and set to draw
  canvas.setBrightness(DEFAULT_BRIGHTNESS);
  canvas.fillCanvas(fullCanvas, INITIAL_CANVAS_COLOR);
  canvas.setDrawState(true);

  canvas.setAnimate(true);
  canvas.setAnimationType(AnimationType::MovingDot);



  // Connect to wifi
  Serial.println("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while(WiFi.status() != WL_CONNECTED)
  {
    delay(WIFI_CONNECTION_RETRY_DELAY);
    Serial.print(".");
  }

  // Print IP address
  Serial.println("");
  Serial.println("Wifi Connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize OTA updates
  ArduinoOTA.setHostname("segmentui-led");
  ArduinoOTA.setPassword(CONFIG_OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    canvas.setAnimate(false);  // Stop animations during update
    Serial.println("OTA Update starting...");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("OTA Update complete!");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
  });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");

  // Initialize Kasa device manager
  kasaManager.begin();

  // Set up server

  // index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
      request->send_P(200, "text/html", index_html);
    });

  // canvas size
  server.on("/get-canvas-size", HTTP_GET, [](AsyncWebServerRequest *request) {
    #if PRINT_SERVER_REQUESTS
    Serial.println("Canvas size requested");
    Serial.println("Canvas l x w = " + String(NUM_ROWS) + "x" + String(ROW_LENGTH));
    #endif
    request->send(200, "text/plain", "Canvas size is " + String(NUM_ROWS) + "x" + String(ROW_LENGTH));
  });

  // set right color
  server.on("/set-right-color", HTTP_GET, [](AsyncWebServerRequest *request) {
    #if PRINT_SERVER_REQUESTS
    Serial.println("Setting right colors");
    #endif
    if (request->hasParam("right-color")) {
      #if PRINT_SERVER_PARAMS
      Serial.println("Has right color param..");
      #endif
      String colorName = request->getParam("right-color")->value();
      CRGB rightColor;

      if (colorName == "Red") {
        rightColor = CRGB::Red;
      } else if (colorName == "Green") {
        rightColor = CRGB::Green;
      } else if (colorName == "Blue") {
        rightColor = CRGB::Blue;
      } else {
        rightColor = CRGB::Black; // Default to black if color is not recognized
      }

      //flag canvas to redraw
      canvas.fillCanvas(rightHalf, rightColor);
      canvas.setDrawState(true);


      request->send(200, "text/plain", "Right color set to " + colorName);
    } else {
      Serial.println("No right color param");
      request->send(400, "text/plain", "Missing color parameter");
    }
  });

  // set all color
  server.on("/set-all-color", HTTP_GET, [] (AsyncWebServerRequest *request) {
    #if PRINT_SERVER_REQUESTS
      Serial.println("Setting left colors");
      Serial.println("params" + String(request->params()));
    #endif
    String color; 
    const String leftColorInput = "leftcolor";

    if (request->hasParam("r") && request->hasParam("g") && request->hasParam("b")) {
      #if PRINT_SERVER_PARAMS
        Serial.println("Has left color param..");
      #endif
      //color = request->getParam(r)->value();
      CRGB newColor {(uint8_t)request->getParam("r")->value().toInt(), (uint8_t)request->getParam("g")->value().toInt(), (uint8_t)request->getParam("b")->value().toInt()};
      currentColor = newColor;  // Track current color
      #if PRINT_SERVER_PARAMS
      Serial.println("Color: " + String(newColor.r) + ", " + String(newColor.g) + ", " + String(newColor.b));
      #endif

      // flag canvas to redraw
      canvas.fillCanvas(fullCanvas, newColor);
      canvas.setDrawState(true);

      request->send(200, "text/plain", "Left color set to RGB(" + String(newColor.r) + "," + String(newColor.g) + "," + String(newColor.b) + ")");
    } else {
      Serial.println("No left color param");
      request->send(400, "text/plain", "Missing color parameter");
    }
  });

  // Toggle all LEDs
  server.on("/toggle-all-leds", HTTP_GET, [](AsyncWebServerRequest *request) {
    #if PRINT_SERVER_REQUESTS
      Serial.println("Toggled ALL LEDs");
    #endif
    canvas.setBrightness(canvas.getCurrentBrightness() == 0 ? canvas.getLastBrightness() : 0);
    canvas.setDrawState(true);
    request->send(200, "text/plain", "Toggled LEDs");
  });

  // Toggle LED
  server.on("/toggle-led",HTTP_GET, [](AsyncWebServerRequest *request) {
  #if PRINT_SERVER_REQUESTS
    Serial.println("Toggle-led, request:");
  #endif
    if(request->hasParam("x") && request->hasParam("y")) {
      int x = request->getParam("x")->value().toInt();
      int y = request->getParam("y")->value().toInt();
      #if PRINT_SERVER_PARAMS
      Serial.println("Has x and y params");
      Serial.println("Toggling LED at " + String(x) + "," + String(y));
      #endif

      // toggle LED
      canvas.fillPixel(x, y, CRGB::Red);
      canvas.setDrawState(true);
    
      request->send(200, "text/plain", "Toggled LED at " + String(x) + "," + String(y));
    } else {
      request->send(400, "text/plain", "Missing x or y parameter");
    }
    request->send(200, "text/plain", "Toggled LED");
  });

  // Set brightness
  server.on("/set-brightness", HTTP_GET, [](AsyncWebServerRequest *request) {
    #if PRINT_SERVER_REQUESTS
      Serial.println("Setting brightness");
    #endif
    if (request->hasParam("brightness")) {
      int brightness = request->getParam("brightness")->value().toInt();
      canvas.setBrightness(brightness);
      canvas.setDrawState(true);
      request->send(200, "text/plain", "Brightness set to " + String(brightness));
    } else {
      request->send(400, "text/plain", "Missing brightness parameter");
    }
  });

  // Toggle animation
  server.on("/toggle-animation", HTTP_GET, [](AsyncWebServerRequest *request) {
    #if PRINT_SERVER_REQUESTS
      Serial.println("Toggling animation");
    #endif
    canvas.setAnimate(!canvas.animating());
    request->send(200, "text/plain", "Toggled animation");
  });

  // Select Animation
  server.on("/set-animation", HTTP_GET, [](AsyncWebServerRequest *request) {
    #if PRINT_SERVER_REQUESTS
      Serial.println("Setting animation");
    #endif
    if (request->hasParam("animation")) {

      int animation = request->getParam("animation")->value().toInt();
      currentAnimation = animation;  // Track current animation
      #if PRINT_SERVER_PARAMS
      Serial.println("Animation: " + String(animation));
      #endif

      switch(animation)
      {
        case 0:
          canvas.setAnimationType(AnimationType::Blink);
          break;
        case 1:
          canvas.setAnimationType(AnimationType::MovingDot);
          break;
        case 2:
          canvas.setAnimationType(AnimationType::RandomColorDots);
          break;
        case 3:
          canvas.setAnimationType(AnimationType::ColorWave);
          break;
        case 4:
          canvas.setAnimationType(AnimationType::RandomColorDotsWave);
          break;
        case 5:
          canvas.setAnimationType(AnimationType::RandomColorColumnsWave);
          break;
        case 6:
          canvas.setAnimationType(AnimationType::RotatingThirds);
          break;
        case 7:
          canvas.setAnimationType(AnimationType::RandomWaterfall);
          break;  
        case 8:
          canvas.setAnimationType(AnimationType::OppositeColorDots);
          break;
        default:
          canvas.setAnimationType(AnimationType::Blink);
          break;
      }
      
      request->send(200, "text/plain", "Set animation to " + String(animation));
    } else {
      request->send(400, "text/plain", "Missing animation parameter");
    }
  });

  // Status endpoint - returns JSON with current state
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"brightness\":" + String(canvas.getCurrentBrightness()) + ",";
    json += "\"animating\":" + String(canvas.animating() ? "true" : "false") + ",";
    json += "\"ledsOn\":" + String(canvas.getCurrentBrightness() > 0 ? "true" : "false") + ",";
    json += "\"r\":" + String(currentColor.r) + ",";
    json += "\"g\":" + String(currentColor.g) + ",";
    json += "\"b\":" + String(currentColor.b) + ",";
    json += "\"animation\":" + String(currentAnimation);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Get preset
  server.on("/preset/get", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("id")) {
      int id = request->getParam("id")->value().toInt();
      if (id >= 0 && id < NUM_PRESETS) {
        Preset& p = presets[id];
        String json = "{";
        json += "\"r\":" + String(p.r) + ",";
        json += "\"g\":" + String(p.g) + ",";
        json += "\"b\":" + String(p.b) + ",";
        json += "\"brightness\":" + String(p.brightness) + ",";
        json += "\"animation\":" + String(p.animation) + ",";
        json += "\"animating\":" + String(p.animating ? "true" : "false");
        json += "}";
        request->send(200, "application/json", json);
        return;
      }
    }
    request->send(400, "text/plain", "Invalid preset id");
  });

  // Load preset - applies preset to current state
  server.on("/preset/load", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("id")) {
      int id = request->getParam("id")->value().toInt();
      if (id >= 0 && id < NUM_PRESETS) {
        Preset& p = presets[id];
        currentColor = CRGB(p.r, p.g, p.b);
        currentAnimation = p.animation;
        canvas.setBrightness(p.brightness);
        canvas.fillCanvas(fullCanvas, currentColor);
        canvas.setAnimate(p.animating);
        // Set animation type
        switch(p.animation) {
          case 0: canvas.setAnimationType(AnimationType::Blink); break;
          case 1: canvas.setAnimationType(AnimationType::MovingDot); break;
          case 2: canvas.setAnimationType(AnimationType::RandomColorDots); break;
          case 3: canvas.setAnimationType(AnimationType::ColorWave); break;
          case 4: canvas.setAnimationType(AnimationType::RandomColorDotsWave); break;
          case 5: canvas.setAnimationType(AnimationType::RandomColorColumnsWave); break;
          case 6: canvas.setAnimationType(AnimationType::RotatingThirds); break;
          case 7: canvas.setAnimationType(AnimationType::RandomWaterfall); break;
          default: canvas.setAnimationType(AnimationType::Blink); break;
        }
        canvas.setDrawState(true);
        request->send(200, "text/plain", "Loaded preset " + String(id));
        return;
      }
    }
    request->send(400, "text/plain", "Invalid preset id");
  });

  // Save preset - saves current state to preset
  server.on("/preset/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("id")) {
      int id = request->getParam("id")->value().toInt();
      if (id >= 0 && id < NUM_PRESETS) {
        presets[id].r = currentColor.r;
        presets[id].g = currentColor.g;
        presets[id].b = currentColor.b;
        presets[id].brightness = canvas.getCurrentBrightness();
        presets[id].animation = currentAnimation;
        presets[id].animating = canvas.animating();
        savePreset(id);
        request->send(200, "text/plain", "Saved preset " + String(id));
        return;
      }
    }
    request->send(400, "text/plain", "Invalid preset id");
  });

  // Kasa device endpoints
  server.on("/kasa/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    int count = kasaManager.scanDevices();
    String json = "{\"count\":" + String(count) + ",\"devices\":" + kasaManager.getDevicesJson() + "}";
    request->send(200, "application/json", json);
  });

  server.on("/kasa/devices", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Return cached state without blocking network calls
    request->send(200, "application/json", kasaManager.getDevicesJson());
  });

  server.on("/kasa/refresh", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Manual refresh - can be slow
    kasaManager.refreshStates();
    request->send(200, "application/json", kasaManager.getDevicesJson());
  });

  server.on("/kasa/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("id")) {
      int id = request->getParam("id")->value().toInt();
      bool success = kasaManager.toggleDevice(id);
      String json = "{\"success\":" + String(success ? "true" : "false") + "}";
      request->send(200, "application/json", json);
    } else {
      request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing id parameter\"}");
    }
  });

  // Claude Code status endpoints
  // Set row state: GET /claude/row?row=N&state=STATE[&color=RRGGBB]
  server.on("/claude/row", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("row") || !request->hasParam("state")) {
      request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing row or state parameter\"}");
      return;
    }

    int row = request->getParam("row")->value().toInt();
    String stateStr = request->getParam("state")->value();

    if (row < 0 || row >= NUM_ROWS) {
      request->send(400, "application/json", "{\"success\":false,\"error\":\"Row must be 0-4\"}");
      return;
    }

    ClaudeState state = ClaudeStatus::parseState(stateStr);

    // Check for optional custom color
    if (request->hasParam("color")) {
      String colorStr = request->getParam("color")->value();
      // Parse hex color (RRGGBB format)
      long colorValue = strtol(colorStr.c_str(), NULL, 16);
      CRGB color((colorValue >> 16) & 0xFF, (colorValue >> 8) & 0xFF, colorValue & 0xFF);
      claudeStatus.setRowState(row, state, color);
    } else {
      claudeStatus.setRowState(row, state);
    }

    // When Claude mode is active, immediately apply to LEDs
    if (claudeStatus.isActive()) {
      claudeStatus.applyToLeds();
      FastLED.show();
    }

    String json = "{\"success\":true,\"row\":" + String(row) + ",\"state\":\"" + stateStr + "\"}";
    request->send(200, "application/json", json);
  });

  // Clear all Claude rows: GET /claude/clear
  server.on("/claude/clear", HTTP_GET, [](AsyncWebServerRequest *request) {
    claudeStatus.clearAll();
    FastLED.show();
    request->send(200, "application/json", "{\"success\":true}");
  });

  // Get Claude status: GET /claude/status
  server.on("/claude/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"active\":" + String(claudeStatus.isActive() ? "true" : "false") + ",\"rows\":[";
    for (int i = 0; i < NUM_ROWS; i++) {
      ClaudeState state = claudeStatus.getRowState(i);
      String stateStr;
      switch (state) {
        case ClaudeState::Idle: stateStr = "idle"; break;
        case ClaudeState::Thinking: stateStr = "thinking"; break;
        case ClaudeState::Tool: stateStr = "tool"; break;
        case ClaudeState::Waiting: stateStr = "waiting"; break;
        case ClaudeState::Error: stateStr = "error"; break;
        default: stateStr = "offline"; break;
      }
      json += "\"" + stateStr + "\"";
      if (i < NUM_ROWS - 1) json += ",";
    }
    json += "]}";
    request->send(200, "application/json", json);
  });

  // Start server
  server.begin();
}
 
void loop()
{
  ArduinoOTA.handle();  // Handle OTA updates

  #if PRINT_CANVAS_DRAW_LOOP
    Serial.println("Drawing canvas");
  #endif

  // When Claude status mode is active, it takes over LED control
  if (claudeStatus.isActive()) {
    claudeStatus.update();
    FastLED.show();
  }
  else if(canvas.animating())
  {
    canvas.animate();
  }
  else if(canvas.drawNewState())
  {
    canvas.draw();
  }


  // delay to control frame rate
  #if DELAY_REDRAW
    delay(REDRAW_DELAY_MS);
  #endif

}
 