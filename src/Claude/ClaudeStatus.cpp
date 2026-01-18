#include "ClaudeStatus.h"

extern CRGB leds[];

ClaudeStatus::ClaudeStatus() {
    lastUpdateTime = 0;
    animationPhase = 0.0;

    // Initialize all rows to offline
    for (int i = 0; i < NUM_ROWS; i++) {
        rowStates[i] = ClaudeState::Offline;
        rowColors[i] = CRGB::Black;
        useCustomColor[i] = false;
    }
}

void ClaudeStatus::setRowState(int row, ClaudeState state) {
    if (row < 0 || row >= NUM_ROWS) return;
    rowStates[row] = state;
    useCustomColor[row] = false;
}

void ClaudeStatus::setRowState(int row, ClaudeState state, CRGB color) {
    if (row < 0 || row >= NUM_ROWS) return;
    rowStates[row] = state;
    rowColors[row] = color;
    useCustomColor[row] = true;
}

ClaudeState ClaudeStatus::getRowState(int row) {
    if (row < 0 || row >= NUM_ROWS) return ClaudeState::Offline;
    return rowStates[row];
}

void ClaudeStatus::clearAll() {
    for (int i = 0; i < NUM_ROWS; i++) {
        rowStates[i] = ClaudeState::Offline;
        useCustomColor[i] = false;
    }
    applyToLeds();
}

bool ClaudeStatus::isActive() {
    for (int i = 0; i < NUM_ROWS; i++) {
        if (rowStates[i] != ClaudeState::Offline) {
            return true;
        }
    }
    return false;
}

ClaudeState ClaudeStatus::parseState(const String& stateStr) {
    if (stateStr == "idle") return ClaudeState::Idle;
    if (stateStr == "thinking") return ClaudeState::Thinking;
    if (stateStr == "tool") return ClaudeState::Tool;
    if (stateStr == "waiting") return ClaudeState::Waiting;
    if (stateStr == "error") return ClaudeState::Error;
    return ClaudeState::Offline;
}

CRGB ClaudeStatus::getStateColor(ClaudeState state) {
    switch (state) {
        case ClaudeState::Idle:     return CLAUDE_COLOR_IDLE;
        case ClaudeState::Thinking: return CLAUDE_COLOR_THINKING;
        case ClaudeState::Tool:     return CLAUDE_COLOR_TOOL;
        case ClaudeState::Waiting:  return CLAUDE_COLOR_WAITING;
        case ClaudeState::Error:    return CLAUDE_COLOR_ERROR;
        case ClaudeState::Offline:
        default:                    return CLAUDE_COLOR_OFFLINE;
    }
}

uint8_t ClaudeStatus::getAnimatedBrightness(ClaudeState state, float phase) {
    switch (state) {
        case ClaudeState::Thinking: {
            // Slow pulse: sine wave between 50% and 100%
            float brightness = 0.5 + 0.5 * sin(phase * 2 * PI);
            return (uint8_t)(brightness * 255);
        }
        case ClaudeState::Waiting: {
            // Breathing: smooth sine wave between 20% and 100%
            float brightness = 0.2 + 0.8 * sin(phase * 2 * PI);
            return (uint8_t)(brightness * 255);
        }
        case ClaudeState::Error: {
            // Fast blink: on/off
            return (phase < 0.5) ? 255 : 0;
        }
        case ClaudeState::Idle:
        case ClaudeState::Tool:
        default:
            // Solid - full brightness
            return 255;
    }
}

void ClaudeStatus::fillRow(int row, CRGB color, uint8_t brightness) {
    if (row < 0 || row >= NUM_ROWS) return;

    // Calculate start index for this row
    // Row 0 starts at LED 0, row 1 at LED 60, etc.
    int startIndex = row * ROW_LENGTH;

    // Apply brightness scaling
    CRGB scaledColor = color;
    scaledColor.nscale8(brightness);

    // Fill the entire row
    for (int i = 0; i < ROW_LENGTH; i++) {
        leds[startIndex + i] = scaledColor;
    }
}

void ClaudeStatus::update() {
    unsigned long currentTime = millis();

    // Update animation phase based on time
    // Different states have different animation speeds
    unsigned long deltaTime = currentTime - lastUpdateTime;
    lastUpdateTime = currentTime;

    // Advance phase (normalized 0-1)
    animationPhase += (float)deltaTime / 1000.0;  // In seconds
    if (animationPhase > 10.0) animationPhase -= 10.0;  // Prevent overflow

    applyToLeds();
}

void ClaudeStatus::applyToLeds() {
    for (int row = 0; row < NUM_ROWS; row++) {
        ClaudeState state = rowStates[row];

        if (state == ClaudeState::Offline) {
            fillRow(row, CRGB::Black, 0);
            continue;
        }

        // Get base color
        CRGB color = useCustomColor[row] ? rowColors[row] : getStateColor(state);

        // Calculate phase for this state's animation speed
        float statePhase;
        switch (state) {
            case ClaudeState::Thinking:
                statePhase = fmod(animationPhase * 1000.0 / CLAUDE_PULSE_SPEED, 1.0);
                break;
            case ClaudeState::Waiting:
                statePhase = fmod(animationPhase * 1000.0 / CLAUDE_BREATHE_SPEED, 1.0);
                break;
            case ClaudeState::Error:
                statePhase = fmod(animationPhase * 1000.0 / CLAUDE_BLINK_SPEED, 1.0);
                break;
            default:
                statePhase = 0;
        }

        uint8_t brightness = getAnimatedBrightness(state, statePhase);
        fillRow(row, color, brightness);
    }
}
