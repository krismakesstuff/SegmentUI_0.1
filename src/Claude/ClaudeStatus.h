#ifndef CLAUDE_STATUS_H
#define CLAUDE_STATUS_H

#include <Arduino.h>
#define FASTLED_INTERNAL
#include <FastLED.h>
#include "../Globals.h"

// Claude session states
enum class ClaudeState {
    Offline,    // Session not active - LEDs off
    Idle,       // Session active, waiting for input - dim blue
    Thinking,   // Processing request - yellow pulse
    Tool,       // Running a tool - green solid
    Waiting,    // Waiting for user approval - cyan breathing
    Error       // Error occurred - red blink
};

// State colors (as defined in plan)
#define CLAUDE_COLOR_OFFLINE CRGB::Black
#define CLAUDE_COLOR_IDLE    CRGB(0x00, 0x11, 0x33)  // Dim blue
#define CLAUDE_COLOR_THINKING CRGB(0xFF, 0xAA, 0x00) // Yellow
#define CLAUDE_COLOR_TOOL    CRGB(0x00, 0xFF, 0x00)  // Green
#define CLAUDE_COLOR_WAITING CRGB(0x00, 0xFF, 0xFF)  // Cyan
#define CLAUDE_COLOR_ERROR   CRGB(0xFF, 0x00, 0x00)  // Red

// Animation speeds (ms)
#define CLAUDE_PULSE_SPEED 1500    // Slow pulse for thinking
#define CLAUDE_BREATHE_SPEED 2000  // Breathing for waiting
#define CLAUDE_BLINK_SPEED 300     // Fast blink for error

class ClaudeStatus {
public:
    ClaudeStatus();

    // Set state for a specific row (0-4)
    void setRowState(int row, ClaudeState state);

    // Set state with custom color
    void setRowState(int row, ClaudeState state, CRGB color);

    // Get current state for a row
    ClaudeState getRowState(int row);

    // Clear all rows (set to offline)
    void clearAll();

    // Update animations - call from loop()
    void update();

    // Check if Claude status mode is active (any row not offline)
    bool isActive();

    // Apply current states to LED array
    void applyToLeds();

    // Parse state string to enum
    static ClaudeState parseState(const String& stateStr);

    // Get color for a state
    static CRGB getStateColor(ClaudeState state);

private:
    ClaudeState rowStates[NUM_ROWS];
    CRGB rowColors[NUM_ROWS];       // Custom colors (optional override)
    bool useCustomColor[NUM_ROWS];  // Whether to use custom color
    unsigned long lastUpdateTime;
    float animationPhase;           // 0.0 to 1.0 for smooth animations

    // Calculate animated brightness for a state
    uint8_t getAnimatedBrightness(ClaudeState state, float phase);

    // Fill a specific row with color and brightness
    void fillRow(int row, CRGB color, uint8_t brightness = 255);
};

#endif // CLAUDE_STATUS_H
