#include "Canvas.h"

//=================================================================================================

namespace Animation
{

    int animationFrameDelayMs = 70; // in ms, smaller is faster animation speed
    int currentPosition = 0;
    int columnPosition = 0;
    int currentThirds = 0;
    int thirdLength = ROW_LENGTH / 3;
    int currentHue = 0;
    
    // blink the first pixel
    void blink(CRGB color, int speed)
    {
        // set the current pixel to the color
        leds[currentPosition] = color;
        // show the new drawn state
        FastLED.show();
        // wait for the speed
        delay(speed);
        // set the current pixel to black
        leds[currentPosition] = CRGB::Black;
        // show the new drawn state
        FastLED.show();
        // wait for the speed
        delay(speed);
    }

    // move a single pixel from the start to the end of the strip
    void movingDot(CRGB color, int speed)
    {
       // set the current pixel to black
        leds[currentPosition] = CRGB::Black;
        // increment the current position
        currentPosition++;
        // set the new pixel to the color
        leds[currentPosition] = color;
        // show the new drawn state
        FastLED.show();
        // wait for the speed
        delay(speed);

        // if we are at the end of the strip, reset the position
        if(currentPosition == NUM_LEDS)
        {
            currentPosition = 0;
        }

    }

    // picks a random pixel and changes its color to a random color
    void randomColorDots(int numDots, int speed)
    {
        // get numDots random pixels
        int randomPixels[numDots];
        for(int i = 0; i < numDots; i++)
        {
            randomPixels[i] = random(0, NUM_LEDS);
        }

        // set the random pixels to random colors
        for(int i = 0; i < numDots; i++)
        {
            // get random color
            CRGB color = CHSV(random(0, 255), 255, 255);
            // set pixel to color
            leds[randomPixels[i]] = color;
        }

        // show the new drawn state
        FastLED.show();
    }

    // makes a color wave by shifting the colors of the pixels across the strip
    void colorWave(int speed)
    {
        // get the first pixel
        CRGB firstPixel = leds[0];
        // shift the colors of the pixels
        for(int i = 0; i < NUM_LEDS - 1; i++)
        {
            leds[i] = leds[i + 1];
        }
        // set the last pixel to the first pixel
        leds[NUM_LEDS - 1] = firstPixel;
        // show the new drawn state
        FastLED.show();
        // wait for the speed
        delay(speed);
    }

    void randomColorDotsWave(int speed)
    {
        // draw random color dots
        randomColorDots(5, speed);

        // add random brightness wave on top
        for(int i = 0; i < NUM_LEDS; i++)
        {
            // multiple brightness by random value
            CRGB color = leds[i];
            // get random number
            int randomBrightness = random(0, 255);
            CRGB newColor = CRGB(color.r - randomBrightness, color.g - randomBrightness, color.b - randomBrightness);
            // set pixel to new color
            leds[i] = newColor;
        }

        // show the new drawn state
        FastLED.show();

    }

    // make a random color, assign it to a column, then shift the column across the canvas
    void randomColorColumnsWave(int speed)
    {
        // get random color
        CRGB color = CHSV(random(0, 255), 255, 255);

        // assign the color to a column
        for(int i = 0; i < NUM_ROWS; i++)
        {
            int pixel = canvasArr[i][columnPosition];
            leds[pixel - 1] = CRGB::Black;
            leds[pixel] = color;
        }

        // increment the column position
        columnPosition++;
        if(columnPosition == ROW_LENGTH)
        {
            columnPosition = 0;
        }

        // show the new drawn state
        FastLED.show();
        // wait for the speed
        delay(speed);
    }

    // make a rotating thirds animation
    void rotatingThirds(int speed)
    {
        // get random color
        CRGB color = CHSV(currentHue, 255, 255);

        int currentIndex = currentThirds * thirdLength;
        
        // assign the color to the current third
        for(int i = 0; i < NUM_ROWS; i++)
        {
            for(int j = currentIndex; j < currentIndex + thirdLength; j++)
            {
                int pixel = canvasArr[i][j];
                leds[pixel] = color;
            }
        }

        // increment the current third
        currentThirds++;
        if(currentThirds == 3)
        {
            currentThirds = 0;
        }

        currentHue += 10;
        if(currentHue == 255)
        {
            currentHue = 0;
        }

        // show the new drawn state
        FastLED.show();
        // wait for the speed
        delay(speed);
    }

    bool waterfallInit = true;
    int rowIndex = NUM_ROWS - 1;
    // random color dots will fall from the top of the canvas to the bottom
    void randomWaterfall(int speed)
    {
        if(waterfallInit)
        {
            waterfallInit = false;
            currentPosition = rowIndex;
            FastLED.clear();
            FastLED.show();
        } 
        else 
        {
            int oldPosition = rowIndex;

            if(currentPosition + 1 < rowIndex)
            {
                oldPosition = currentPosition + 1;
            }

            // shift the current waterfall down one row
            for(int i = 0; i < ROW_LENGTH; i++)
            {
                int oldLed = canvasArr[oldPosition][i];
                CRGB oldColor = leds[oldLed];

                int newPosition = canvasArr[currentPosition][i];
                leds[newPosition] = oldColor;
            }
        }

        // get random color
        CRGB color = CHSV(random(0, 255), random(0, 255), 255);

        // get a random column
        int randomColumn = random(0, ROW_LENGTH);
        int pixel = canvasArr[currentPosition][randomColumn];

        // set the pixel to the color
        leds[pixel] = color;

        // increment the current position
        currentPosition--;
        if(currentPosition < 0)
        {
            currentPosition = rowIndex;
        }

        // show the new drawn state
        FastLED.show();
        // wait for the speed
        delay(speed);
    }

    // opposite color dots variables
    bool oppositeColorDotsInit = true;
    int collisionDistance = NUM_LEDS / 2;
    int currentStep = 0;
    int lastPixel = NUM_LEDS - 1;

    int bottomPixelPosition = 0;
    int topPixelPosition = lastPixel;

    int direction = 1; // 1 = towards, -1 = away
    
    // make dots of opposite colors. Start them at opposite ends of the strip and move them towards each other. Once they meet, they will change color and reverse movement direction
    void oppositeColorDots(int speed)
    {
        if(oppositeColorDotsInit)
        {
            oppositeColorDotsInit = false;
            // reset to intial values. Used when the animation is called again
            currentStep = 0;
            direction = 1;
            bottomPixelPosition = 0;
            topPixelPosition = lastPixel;

            FastLED.clear();
            FastLED.show();

            // get random color
            CRGB color = CRGB::Blue;

            // make the opposite color of the random color
            CRGB oppositeColor = CRGB::Red;

            // set the last pixel to color
            leds[topPixelPosition] = color;

            // set the first pixel to opposite color
            leds[bottomPixelPosition] = oppositeColor;

        } else
        {

            leds[topPixelPosition] = leds[topPixelPosition + 1];
            leds[topPixelPosition + 1] = CRGB::Black;

            leds[bottomPixelPosition] = leds[bottomPixelPosition - 1];
            leds[bottomPixelPosition - 1] = CRGB::Black;

            //Serial.println("topPixelPosition = " + String(topPixelPosition) + ", bottomPixelPosition = " + String(bottomPixelPosition));
        }

        topPixelPosition--;
        bottomPixelPosition++;
        
        if(bottomPixelPosition == collisionDistance || topPixelPosition == collisionDistance)
        {
            bottomPixelPosition = 0;
            topPixelPosition = lastPixel;
        }


        // show the new drawn state
        FastLED.show();
        // wait for the speed
        delay(speed);

    }


} // namespace Animation

//=================================================================================================

Canvas::Canvas()
{}

Canvas::~Canvas()
{
    Serial.print("Canvas Dtor");
}

// must set drawnewstate to true after calling this method for it to show
void Canvas::setBrightness(int brightness)
{
    if(brightness != 0)
        lastBrightness = brightness;
    
    FastLED.setBrightness(brightness);
}

int Canvas::getCurrentBrightness()
{
    return FastLED.getBrightness();
}

int Canvas::getLastBrightness()
{
    return lastBrightness;
}

// call before using the canvas
void Canvas::buildCanvas(int numRows, int rowLength, bool serp)
{
    //load every odd row in reverse and even row forward
    for(int row = 0; row < numRows; row++)
    {
        if(row % 2 == 0)
        {
            int rowStart = row * rowLength;
            for(int i = 0; i < rowLength; i++)
            {
                canvasArr[row][i] = rowStart + i;
            }
        }
        else
        {
            int rowStart = (1 + row) * rowLength - 1;
            for(int j = 0; j < rowLength; j++)
            {
                canvasArr[row][j] = rowStart - j;
            }
        }
    }

#if PRINT_CANVAS_BUILD
    Serial.println("canvasArr size = " + String(sizeof(canvasArr)));
    for(int y = 0; y < NUM_ROWS; y++)
    {
        for(int x = 0; x < ROW_LENGTH; x++)
        {
            Serial.println("x = " + String(x) + ", y = " + String(y) + ", value = " + String(canvasArr[y][x]));
        }
    }
#endif
}

// call in the main loop
bool Canvas::drawNewState()
{
    return redrawState;
}

// call to trigger a redraw in the main loop
// it will be flipped back to false in draw()
void Canvas::setDrawState(bool wantsToDraw)
{
    redrawState = wantsToDraw;
}

void Canvas::clear()
{
    FastLED.clear();
}

void Canvas::setAnimate(bool wantsToAnimate)
{
    drawAnimation = wantsToAnimate;
}

bool Canvas::animating()
{
    return drawAnimation;
}

//private

// set variables for redraw
void Canvas::fillCanvas(Rectangle area, CRGB color)
{
    areaToDraw = area;
    colorToDraw = color;

    #if PRINT_CANVAS_FILL
    Serial.println("Filling canvas with color " + String(color.r) + ", " + String(color.g) + ", " + String(color.b));
    Serial.println("Area to draw: x = " + String(area._x) + ", y = " + String(area._y) + ", width = " + String(area._width) + ", height = " + String(area._height));
    #endif
}

// set variables for redraw a one pixel. Still need to set drawNewState to true
void Canvas::fillPixel(int x, int y, CRGB color)
{
    if(x < 0 || x >= ROW_LENGTH || y < 0 || y >= NUM_ROWS)
    {
        Serial.println("Invalid pixel coordinates");
        Serial.println("x = " + String(x) + ", y = " + String(y));
        return;
    }

    pixelToDraw._color = color;
    pixelToDraw._x = x;
    pixelToDraw._y = y;

    updatePixelOnly = true; 
}

// main loop draw
void Canvas::draw()
{
    // we are about to draw, so set the state to false
    setDrawState(false);

    
    if(updatePixelOnly) // change the state of a single pixel
    {
        // flip the flag
        updatePixelOnly = false;
        // draw the pixel
        int pixel = canvasArr[pixelToDraw._y][pixelToDraw._x];
        leds[pixel] = pixelToDraw._color;
        // show the new drawn state
        FastLED.show();
    } 
    else // change the state of an area
    {
        // if((areaToDraw._width == 0 || areaToDraw._height == 0) || (colorToDraw.r == 0 && colorToDraw.g == 0 && colorToDraw.b == 0))
        // {
        //     // nothing to draw
        //     return;
        // }   

        // draw the area
        for(int y = areaToDraw._y; y < areaToDraw._y + areaToDraw._height; y++)
        {
            for(int x = areaToDraw._x; x < areaToDraw._x + areaToDraw._width; x++)
            {
                int pixel = canvasArr[y][x];
                leds[pixel] = colorToDraw;

            }
        }

        // show the new drawn state
        FastLED.show();
    }

}

void Canvas::animate()
{
    switch(currentAnimationType)
    {
        case AnimationType::Blink:
            Animation::blink(CRGB::Red, Animation::animationFrameDelayMs);
            break;
        case AnimationType::MovingDot:
            Animation::movingDot(CRGB::Red, Animation::animationFrameDelayMs);
            break;
        case AnimationType::RandomColorDots:
            Animation::randomColorDots(5, Animation::animationFrameDelayMs);
            break;
        case AnimationType::ColorWave:
            Animation::colorWave(Animation::animationFrameDelayMs);
            break;
        case AnimationType::RandomColorDotsWave:
            Animation::randomColorDotsWave(Animation::animationFrameDelayMs);
            break;
        case AnimationType::RandomColorColumnsWave:
            Animation::randomColorColumnsWave(Animation::animationFrameDelayMs);
            break;
        case AnimationType::RotatingThirds:
            Animation::rotatingThirds(Animation::animationFrameDelayMs);
            break;
        case AnimationType::RandomWaterfall:
            Animation::randomWaterfall(Animation::animationFrameDelayMs);
            break;
        case AnimationType::OppositeColorDots:
            Animation::oppositeColorDots(Animation::animationFrameDelayMs);
            break;
        default:
            break;

    }
}

void Canvas::setAnimationType(AnimationType type)
{
    currentAnimationType = type;
}

//=================================================================================================

Rectangle::Rectangle()
{}

Rectangle::Rectangle(int x, int y, int width, int height)
{
    _x = x;
    _y = y;
    _width = width;
    _height = height;
}

//=================================================================================================

Pixel::Pixel()
{}

Pixel::Pixel(int x, int y, CRGB color)
{
    _x = x;
    _y = y;
    _color = color;
}

//=================================================================================================

