#pragma once
#include <cstdint>

// Thin hardware abstraction layer over M5Unified.
//
// Every wrist/ankle target talks to the M5StickC Plus2 exclusively through
// this namespace, so the application layer (src/*/main.cpp) never touches
// M5Unified directly. This keeps main.cpp portable and easy to unit-test
// in isolation from the actual hardware.
namespace Hardware
{
    // ── Lifecycle ──
    void init();
    void update();

    // ── User input ──
    bool isShortPress();
    bool isLongPress();

    // ── Sensors & actuators ──
    float getAccelMagnitude();
    void setBackgroundColor(uint32_t rgbColor);
    void beep(uint32_t frequencyHz, uint32_t durationMs);

    // Centered layout: IDLE, DIAGNOSTIC, PAUSE.
    // leftBat/rightBat: -2 = hidden, -1 = "disconnected", >=0 = battery %.
    void display(const char *header, const char *bodyCenter = "", int leftBat = -2, int rightBat = -2);

    // Split layout (left/right columns): RUNNING_NORMAL, RUNNING_ALERT, CALIBRATION.
    void display(const char *header, const char *bodyLeft, const char *bodyRight, int leftBat = -2, int rightBat = -2);
}
