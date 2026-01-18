#include "ClaudeStatus.h"

extern CRGB leds[];

ClaudeStatus::ClaudeStatus() {
    enabled = false;
    lastUpdateTime = 0;
    animationPhase = 0.0;

    // Initialize all rows to offline
    for (int i = 0; i < NUM_ROWS; i++) {
        rowStates[i] = ClaudeState::Offline;
        rowColors[i] = CRGB::Black;
        useCustomColor[i] = false;
        contextPercent[i] = 0;
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

void ClaudeStatus::setContextPercent(int row, uint8_t percent) {
    if (row < 0 || row >= NUM_ROWS) return;
    contextPercent[row] = min(percent, (uint8_t)100);
}

uint8_t ClaudeStatus::getContextPercent(int row) {
    if (row < 0 || row >= NUM_ROWS) return 0;
    return contextPercent[row];
}

void ClaudeStatus::clearAll() {
    for (int i = 0; i < NUM_ROWS; i++) {
        rowStates[i] = ClaudeState::Offline;
        useCustomColor[i] = false;
        contextPercent[i] = 0;
    }
    applyToLeds();
}

bool ClaudeStatus::isActive() {
    if (!enabled) return false;
    for (int i = 0; i < NUM_ROWS; i++) {
        if (rowStates[i] != ClaudeState::Offline) {
            return true;
        }
    }
    return false;
}

void ClaudeStatus::setEnabled(bool state) {
    enabled = state;
}

bool ClaudeStatus::isEnabled() {
    return enabled;
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
        case ClaudeState::Thinking: return CLAUDE_COLOR_WORKING_A;  // Will be animated
        case ClaudeState::Tool:     return CLAUDE_COLOR_WORKING_A;  // Will be animated
        case ClaudeState::Waiting:  return CLAUDE_COLOR_WAITING;
        case ClaudeState::Error:    return CLAUDE_COLOR_ERROR;
        case ClaudeState::Offline:
        default:                    return CLAUDE_COLOR_OFFLINE;
    }
}

uint8_t ClaudeStatus::getAnimatedBrightness(ClaudeState state, float phase) {
    // Minimum brightness is 50% of max
    const float MIN_BRIGHTNESS = 0.50;

    switch (state) {
        case ClaudeState::Thinking:
        case ClaudeState::Tool: {
            // Working: gentle pulse between 50% and 100% of max
            float brightness = 0.5 + 0.5 * (sin(phase * 2 * PI) + 1.0) / 2.0;
            return (uint8_t)(brightness * CLAUDE_MAX_BRIGHTNESS);
        }
        case ClaudeState::Waiting: {
            // Breathing: subtle sine wave between 70% and 100% of max
            const float WAITING_MIN = 0.70;
            float wave = (sin(phase * 2 * PI) + 1.0) / 2.0;  // 0 to 1
            float brightness = WAITING_MIN + (1.0 - WAITING_MIN) * wave;
            return (uint8_t)(brightness * CLAUDE_MAX_BRIGHTNESS);
        }
        case ClaudeState::Error: {
            // Gentle fade: sine wave between 25% and 100% of max
            float wave = (sin(phase * 2 * PI) + 1.0) / 2.0;  // 0 to 1
            float brightness = MIN_BRIGHTNESS + (1.0 - MIN_BRIGHTNESS) * wave;
            return (uint8_t)(brightness * CLAUDE_MAX_BRIGHTNESS);
        }
        case ClaudeState::Idle: {
            // Blinking: smooth sine wave between 0% and 100% of max
            float wave = (sin(phase * 2 * PI) + 1.0) / 2.0;  // 0 to 1
            return (uint8_t)(wave * CLAUDE_MAX_BRIGHTNESS);
        }
        default:
            return CLAUDE_MAX_BRIGHTNESS;
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

void ClaudeStatus::fillRowWithProgress(int row, CRGB statusColor, uint8_t statusBrightness, uint8_t percent) {
    if (row < 0 || row >= NUM_ROWS) return;

    int startIndex = row * ROW_LENGTH;
    int progressLeds = (percent * ROW_LENGTH) / 100;  // How many LEDs for progress bar

    // Progress bar at constant brightness
    CRGB scaledProgress = CLAUDE_COLOR_PROGRESS;
    scaledProgress.nscale8(CLAUDE_MAX_BRIGHTNESS);

    // Status color with animation brightness
    CRGB scaledStatus = statusColor;
    scaledStatus.nscale8(statusBrightness);

    // Serpentine pattern: even/odd rows have opposite index directions
    // Even rows (0, 2, 4): index 0 = RIGHT, so progress at HIGH indices
    // Odd rows (1, 3): index 0 = LEFT, so progress at LOW indices
    if (row % 2 == 0) {
        // Even row: HIGH indices = LEFT side
        for (int i = ROW_LENGTH - progressLeds; i < ROW_LENGTH; i++) {
            leds[startIndex + i] = scaledProgress;
        }
        for (int i = 0; i < ROW_LENGTH - progressLeds; i++) {
            leds[startIndex + i] = scaledStatus;
        }
    } else {
        // Odd row: LOW indices = LEFT side
        for (int i = 0; i < progressLeds; i++) {
            leds[startIndex + i] = scaledProgress;
        }
        for (int i = progressLeds; i < ROW_LENGTH; i++) {
            leds[startIndex + i] = scaledStatus;
        }
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
        uint8_t percent = contextPercent[row];

        if (state == ClaudeState::Offline) {
            fillRow(row, CRGB::Black, 0);
            continue;
        }

        // Calculate phase for this state's animation speed
        float statePhase;
        CRGB color;

        switch (state) {
            case ClaudeState::Thinking:
            case ClaudeState::Tool: {
                // Working: alternate between green and yellow smoothly
                statePhase = fmod(animationPhase * 1000.0 / CLAUDE_WORK_CYCLE_SPEED, 1.0);
                // Blend between colors using sine wave
                float blendAmount = (sin(statePhase * 2 * PI) + 1.0) / 2.0;  // 0 to 1
                CRGB colorA = CLAUDE_COLOR_WORKING_A;
                CRGB colorB = CLAUDE_COLOR_WORKING_B;
                color = CRGB(
                    colorA.r + (uint8_t)((colorB.r - colorA.r) * blendAmount),
                    colorA.g + (uint8_t)((colorB.g - colorA.g) * blendAmount),
                    colorA.b + (uint8_t)((colorB.b - colorA.b) * blendAmount)
                );
                uint8_t brightness = getAnimatedBrightness(state, statePhase);
                fillRowWithProgress(row, color, brightness, percent);
                continue;
            }
            case ClaudeState::Waiting: {
                statePhase = fmod(animationPhase * 1000.0 / CLAUDE_BREATHE_SPEED, 1.0);
                color = useCustomColor[row] ? rowColors[row] : CLAUDE_COLOR_WAITING;
                uint8_t brightness = getAnimatedBrightness(state, statePhase);
                fillRowWithProgress(row, color, brightness, percent);
                continue;
            }
            case ClaudeState::Error: {
                statePhase = fmod(animationPhase * 1000.0 / CLAUDE_ERROR_FADE_SPEED, 1.0);
                color = useCustomColor[row] ? rowColors[row] : CLAUDE_COLOR_ERROR;
                uint8_t brightness = getAnimatedBrightness(state, statePhase);
                fillRowWithProgress(row, color, brightness, percent);
                continue;
            }
            case ClaudeState::Idle: {
                statePhase = fmod(animationPhase * 1000.0 / CLAUDE_IDLE_BLINK_SPEED, 1.0);
                color = useCustomColor[row] ? rowColors[row] : CLAUDE_COLOR_IDLE;
                uint8_t brightness = getAnimatedBrightness(state, statePhase);

                int startIndex = row * ROW_LENGTH;
                int progressLeds = (percent * ROW_LENGTH) / 100;

                CRGB scaledProgress = CLAUDE_COLOR_PROGRESS;
                scaledProgress.nscale8(CLAUDE_MAX_BRIGHTNESS);

                // Serpentine: even rows HIGH=LEFT, odd rows LOW=LEFT
                if (row % 2 == 0) {
                    // Even row: progress at HIGH indices (left side)
                    for (int i = ROW_LENGTH - progressLeds; i < ROW_LENGTH; i++) {
                        leds[startIndex + i] = scaledProgress;
                    }
                    for (int i = 0; i < ROW_LENGTH - progressLeds; i++) {
                        leds[startIndex + i] = CRGB::Black;
                    }
                } else {
                    // Odd row: progress at LOW indices (left side)
                    for (int i = 0; i < progressLeds; i++) {
                        leds[startIndex + i] = scaledProgress;
                    }
                    for (int i = progressLeds; i < ROW_LENGTH; i++) {
                        leds[startIndex + i] = CRGB::Black;
                    }
                }

                // If context is 100%, just show full green (no idle LED needed)
                if (percent >= 100) {
                    continue;
                }

                // Show single blinking idle LED right after the progress bar
                int idleLedIndex;
                if (row % 2 == 0) {
                    // Even row: idle LED is right before progress starts
                    idleLedIndex = startIndex + (ROW_LENGTH - progressLeds - 1);
                } else {
                    // Odd row: idle LED is right after progress
                    idleLedIndex = startIndex + progressLeds;
                }
                CRGB scaledColor = color;
                scaledColor.nscale8(brightness);
                leds[idleLedIndex] = scaledColor;
                continue;
            }
            default:
                statePhase = 0;
                color = CRGB::Black;
                break;
        }

        uint8_t brightness = getAnimatedBrightness(state, statePhase);
        fillRow(row, color, brightness);
    }
}
