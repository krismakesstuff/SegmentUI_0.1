#ifndef CLAUDE_STATUS_H
#define CLAUDE_STATUS_H

#include <Arduino.h>
#define FASTLED_INTERNAL
#include <FastLED.h>
#include "../Globals.h"

// Claude session states
enum class ClaudeState {
    Offline,    // Session not active - LEDs off
    Idle,       // Session active, waiting for input - single cyan LED blinking
    Thinking,   // Processing request - yellow pulse
    Tool,       // Running a tool - green solid
    Waiting,    // Waiting for user approval - cyan breathing
    Error       // Error occurred - red blink
};

// Max brightness (0-255) - 50% = 128
#define CLAUDE_MAX_BRIGHTNESS 128

// State colors (dimmed for large display)
#define CLAUDE_COLOR_OFFLINE CRGB::Black
#define CLAUDE_COLOR_IDLE    CRGB(0x00, 0xC0, 0xFF)  // Bright cyan (same as waiting, scaled by brightness)
#define CLAUDE_COLOR_WORKING_A CRGB(0x00, 0x80, 0x00) // Green (dimmed)
#define CLAUDE_COLOR_WORKING_B CRGB(0x80, 0x55, 0x00) // Yellow/amber (dimmed)
#define CLAUDE_COLOR_WAITING CRGB(0x00, 0xC0, 0xFF)  // Bright cyan (scaled by brightness)
#define CLAUDE_COLOR_ERROR   CRGB(0x80, 0x00, 0x00)  // Dimmed red

// Animation speeds (ms) - slower for subtlety
#define CLAUDE_WORK_CYCLE_SPEED 4000   // Slow color alternation for working
#define CLAUDE_BREATHE_SPEED 333       // Breathing for waiting
#define CLAUDE_ERROR_FADE_SPEED 3000   // Gentle fade for error
#define CLAUDE_IDLE_BLINK_SPEED 300    // Single LED blink for idle (10% faster than waiting)

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

    // Check if Claude status mode is active (enabled AND any row not offline)
    bool isActive();

    // Enable/disable Claude mode
    void setEnabled(bool state);
    bool isEnabled();

    // Apply current states to LED array
    void applyToLeds();

    // Parse state string to enum
    static ClaudeState parseState(const String& stateStr);

    // Get color for a state
    static CRGB getStateColor(ClaudeState state);

private:
    bool enabled;                   // Whether Claude mode is enabled via UI
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
