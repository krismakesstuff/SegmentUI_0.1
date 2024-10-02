#include <Arduino.h>

#define FASTLED_INTERNAL
#include <FastLED.h>
#include "Globals.h"

//this array stores the indicies of the actual leds, necessary because of the serpentine wiring
// use x,y coordinates to get the index of the led
static int canvasArr[NUM_ROWS][ROW_LENGTH] = {0};

// this array let's assign a color to a pixel. The led strip is 300 leds long, so we can use the x,y coordinates of the canvasArr to get the index of the led in leds[].
extern CRGB leds[];

// Different animations. see Animations namespace in Canvas.cpp for implementation
enum AnimationType
{
    Blink,
    MovingDot,
    RandomColorDots,
    ColorWave,
    RandomColorDotsWave,
    RandomColorColumnsWave,
    RotatingThirds,
    RandomWaterfall,
    OppositeColorDots,
};

//coordinates assume the 0,0 origin is the top right corner
class Rectangle
{
public:

    Rectangle();
    Rectangle(int x, int y, int width, int height);

    int _x = 0;
    int _y = 0;
    int _width = 0;
    int _height = 0;
};

// holds x, y coordinates and color of a pixel. Used by the canvas class to draw individual pixels.
class Pixel
{
public:
    Pixel();
    Pixel(int x, int y, CRGB color);

    int _x = 0;
    int _y = 0;
    CRGB _color = CRGB::Black;
};


//Canvas class represents the "drawable" layer. 
class Canvas 
{
public:
    
    //Canvas(int numRows, int rowLength);
    Canvas();
    ~Canvas();

    void buildCanvas(int numRows, int rowLength, bool serp = true);

    void setBrightness(int brightness);
    int getCurrentBrightness();
    int getLastBrightness();
    
    void fillCanvas(Rectangle area, CRGB color);
    void fillPixel(int x, int y, CRGB color);

    bool drawNewState();
    void setDrawState(bool wantsToDraw);

    void clear();

    void setAnimate(bool wantsToAnimate);
    void setAnimationType(AnimationType type);
    bool animating();

    //void getCanvasSize(int& numRows, int& rowLength);

    void draw();
    void animate();
private:


    bool drawAnimation = false;
    AnimationType currentAnimationType = AnimationType::Blink;

    bool redrawState = false;
    Rectangle lastDrawnRectangle{};
    int lastBrightness = DEFAULT_BRIGHTNESS;
    Rectangle areaToDraw {};
    CRGB colorToDraw = INITIAL_CANVAS_COLOR;

    bool updatePixelOnly = false;
    Pixel pixelToDraw{};
};





