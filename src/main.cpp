#include <Arduino.h>
#define FASTLED_INTERNAL
#include <FastLED.h>

#include <DNSServer.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"

#include "Globals.h"
#include "Canvas/Canvas.h"
#include "index"
#include "credentials.h"


const char* WIFI_SSID = CONFIG_WIFI_SSID;
const char* WIFI_PASSWORD = CONFIG_WIFI_PASSWORD;


//set port
AsyncWebServer server(80);
//http request
String header;

//making LED data
CRGB leds[NUM_LEDS] = {0};
//static int canvasArr[NUM_ROWS][NUM_ROWS];

//create canvas
Canvas canvas{};
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

void setup() 
{
  //connect to serial monitor
  Serial.begin(9600);
  Serial.println("Connected");

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
      CRGB newColor {request->getParam("r")->value().toInt(), request->getParam("g")->value().toInt(), request->getParam("b")->value().toInt()};
      #if PRINT_SERVER_PARAMS
      Serial.println("Color: " + String(newColor.r) + ", " + String(newColor.g) + ", " + String(newColor.b));
      #endif

      // flag canvas to redraw
      canvas.fillCanvas(fullCanvas, newColor);
      canvas.setDrawState(true);

      request->send(200, "text/plain", "Left color set to " + newColor );
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



  // Start server
  server.begin();
}
 
void loop() 
{
  #if PRINT_CANVAS_DRAW_LOOP
    Serial.println("Drawing canvas");
  #endif
  
  
  if(canvas.animating())
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
 