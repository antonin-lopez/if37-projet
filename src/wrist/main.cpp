#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "Protocol.h"
#include "Hardware.h"
#include "GaitAlgorithms.h"

// ─── FreeRTOS ARCHITECTURE ───
// The ESP-NOW receive callback runs on the WiFi/network task, not on the
// main loop task. To avoid touching shared state from two different tasks
// (and all the race conditions that come with it), the callback only ever
// pushes a copy of the incoming message onto `eventQueue`. All actual state
// mutation and rendering happens synchronously inside loop().
enum class EventType
{
    IMPACT,
    HEARTBEAT
};

struct SystemEvent
{
    EventType type;
    union
    {
        ImpactMessage impact;
        HeartbeatMessage heartbeat;
    } data;
};

QueueHandle_t eventQueue = nullptr;

namespace
{
    // How long an ankle unit can go without a heartbeat before it's
    // considered disconnected.
    constexpr uint32_t ANKLE_TIMEOUT_MS = 1500;
    // Minimum interval between two display redraws while running/idle.
    constexpr uint32_t DISPLAY_REFRESH_INTERVAL_MS = 500;
    // How long a side indicator stays on screen in DIAGNOSTIC mode.
    constexpr uint32_t DIAGNOSTIC_FLASH_DURATION_MS = 200;
    constexpr uint32_t LOOP_DELAY_MS = 10;

    // ── Buzzer tones/durations, named so every beep in the state machine
    // is self-documenting instead of a bare magic number at the call site. ──
    constexpr uint32_t TONE_CALIBRATION_STEP_HZ = 1000;
    constexpr uint32_t TONE_CALIBRATION_STEP_MS = 50;
    constexpr uint32_t TONE_DIAGNOSTIC_ENTER_HZ = 1000;
    constexpr uint32_t TONE_DIAGNOSTIC_ENTER_MS = 50;
    constexpr uint32_t TONE_DIAGNOSTIC_IMPACT_HZ = 1000;
    constexpr uint32_t TONE_DIAGNOSTIC_IMPACT_MS = 50;
    constexpr uint32_t TONE_RUNNING_START_HZ = 1500;
    constexpr uint32_t TONE_RUNNING_START_MS = 400;
    constexpr uint32_t TONE_ALERT_HZ = 2000;
    constexpr uint32_t TONE_ALERT_MS = 150;

    struct StateVisuals
    {
        const char *title;
        uint32_t bgColor;
    };

    // Single source of truth for the title/color associated with each
    // SystemState, shared by both updateDisplay() overloads below.
    StateVisuals getStateVisuals(SystemState state)
    {
        switch (state)
        {
        case SystemState::IDLE:
            return {"IDLE", 0x000000};
        case SystemState::DIAGNOSTIC:
            return {"DIAGNOSTIC", 0xFFFF00};
        case SystemState::CALIBRATION:
            return {"CALIBRATION", 0x0000FF};
        case SystemState::RUNNING_NORMAL:
            return {"RUNNING (OK)", 0x00FF00};
        case SystemState::RUNNING_ALERT:
            return {"ASYMMETRY ALERT!", 0xFF0000};
        case SystemState::PAUSE:
            return {"PAUSE", 0xFF00FF};
        }
        return {"", 0x000000};
    }
}

// ─── State (owned exclusively by the main loop task) ───
uint32_t leftHeartbeatTime = 0, rightHeartbeatTime = 0;
uint8_t leftBattery = 0;
uint8_t rightBattery = 0;

SystemState currentState = SystemState::IDLE;
GaitAnalyzer analyzer(DEFAULT_VALIDATION_THRESHOLD_G);
float asymmetry = 0.0f;
uint32_t lastDisplayTime = 0;

// ─── Overload 1: centered layout (IDLE, DIAGNOSTIC, CALIBRATION, PAUSE) ───
void updateDisplay(const char *bodyCenter = nullptr)
{
    uint32_t now = millis();
    int leftBatParam = -2;
    int rightBatParam = -2;

    if (currentState == SystemState::DIAGNOSTIC || currentState == SystemState::PAUSE)
    {
        leftBatParam = (now - leftHeartbeatTime < ANKLE_TIMEOUT_MS) ? leftBattery : -1;
        rightBatParam = (now - rightHeartbeatTime < ANKLE_TIMEOUT_MS) ? rightBattery : -1;
    }

    StateVisuals visuals = getStateVisuals(currentState);
    const char *centerData = (currentState == SystemState::CALIBRATION) ? "STEPS: 0/32" : "";
    if (bodyCenter != nullptr)
        centerData = bodyCenter;

    Hardware::setBackgroundColor(visuals.bgColor);
    Hardware::display(visuals.title, centerData, leftBatParam, rightBatParam);
}

// ─── Overload 2: split layout (used only while RUNNING or CALIBRATION) ───
void updateDisplay(const char *bodyLeft, const char *bodyRight)
{
    StateVisuals visuals = getStateVisuals(currentState);
    Hardware::setBackgroundColor(visuals.bgColor);
    Hardware::display(visuals.title, bodyLeft, bodyRight);
}

// ─── Wireless receive callback (network task - ISR-like, keep it minimal) ───
void onDataReceived(const uint8_t *mac, const uint8_t *data, int len)
{
    if (eventQueue == nullptr)
        return;

    SystemEvent event;

    if (len == sizeof(ImpactMessage))
    {
        event.type = EventType::IMPACT;
        memcpy(&event.data.impact, data, sizeof(ImpactMessage));
        xQueueSend(eventQueue, &event, 0); // Non-blocking: never stall the network stack.
    }
    else if (len == sizeof(HeartbeatMessage))
    {
        event.type = EventType::HEARTBEAT;
        memcpy(&event.data.heartbeat, data, sizeof(HeartbeatMessage));
        xQueueSend(eventQueue, &event, 0);
    }
}

void enterCalibrationState()
{
    analyzer.reset();

    char strLeft[16];
    char strRight[16];
    snprintf(strLeft, sizeof(strLeft), "L: %d", GaitAnalyzer::CALIBRATION_STEPS_PER_SIDE);
    snprintf(strRight, sizeof(strRight), "R: %d", GaitAnalyzer::CALIBRATION_STEPS_PER_SIDE);
    updateDisplay(strLeft, strRight);

    Hardware::beep(TONE_CALIBRATION_STEP_HZ, TONE_CALIBRATION_STEP_MS);
    delay(80);
    Hardware::beep(TONE_CALIBRATION_STEP_HZ, TONE_CALIBRATION_STEP_MS);
}

void transitionTo(SystemState newState)
{
    currentState = newState;

    // A single switch groups every state's "on entry" action.
    switch (currentState)
    {
    case SystemState::CALIBRATION:
        enterCalibrationState();
        break;

    case SystemState::DIAGNOSTIC:
        updateDisplay();
        Hardware::beep(TONE_DIAGNOSTIC_ENTER_HZ, TONE_DIAGNOSTIC_ENTER_MS);
        break;

    case SystemState::RUNNING_NORMAL:
        updateDisplay();
        Hardware::beep(TONE_RUNNING_START_HZ, TONE_RUNNING_START_MS);
        break;

    case SystemState::IDLE:
    case SystemState::RUNNING_ALERT:
    case SystemState::PAUSE:
    default:
        updateDisplay();
        break;
    }

    lastDisplayTime = millis();
}

void setup()
{
    Serial.begin(115200);
    Hardware::init();

    eventQueue = xQueueCreate(20, sizeof(SystemEvent));
    if (eventQueue == nullptr)
    {
        Serial.println("Fatal: failed to create FreeRTOS queue");
        esp_restart();
    }

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Fatal: esp_now_init failed");
        esp_restart();
    }
    esp_now_register_recv_cb(onDataReceived);

    transitionTo(SystemState::IDLE);
}

void loop()
{
    Hardware::update();
    uint32_t now = millis();

    bool btnShort = Hardware::isShortPress();
    bool btnLong = Hardware::isLongPress();
    bool anklesConnected = (now - leftHeartbeatTime < ANKLE_TIMEOUT_MS) && (now - rightHeartbeatTime < ANKLE_TIMEOUT_MS);
    bool refreshDue = (now - lastDisplayTime >= DISPLAY_REFRESH_INTERVAL_MS);
    bool runningImpactProcessed = false;

    // ── STEP 1: drain and process queued events synchronously (zero race conditions) ──
    SystemEvent event;
    while (xQueueReceive(eventQueue, &event, 0) == pdTRUE)
    {
        if (event.type == EventType::IMPACT)
        {
            ImpactMessage msg = event.data.impact;

            if (currentState == SystemState::RUNNING_NORMAL || currentState == SystemState::RUNNING_ALERT)
            {
                // Only steps that clear the wrist's own VALIDATION threshold
                // are trusted for gait metrics (see Protocol.h).
                if (msg.peakForce >= analyzer.getMinForceThreshold())
                {
                    analyzer.addRunningStep(msg.peakForce, msg.isLeft);
                    runningImpactProcessed = true;
                }
            }
            else if (currentState == SystemState::DIAGNOSTIC)
            {
                Hardware::beep(TONE_DIAGNOSTIC_IMPACT_HZ, TONE_DIAGNOSTIC_IMPACT_MS);
                const char *impactSide = msg.isLeft ? "LEFT" : "RIGHT";
                updateDisplay(impactSide);
                delay(DIAGNOSTIC_FLASH_DURATION_MS);
                updateDisplay();
                lastDisplayTime = now;
            }
            else if (currentState == SystemState::CALIBRATION && msg.peakForce >= analyzer.getMinForceThreshold())
            {
                bool calibDone = analyzer.addCalibrationStep(msg.peakForce, msg.isLeft);

                int leftRemaining = GaitAnalyzer::CALIBRATION_STEPS_PER_SIDE - analyzer.getLeftStepCount();
                int rightRemaining = GaitAnalyzer::CALIBRATION_STEPS_PER_SIDE - analyzer.getRightStepCount();

                char strLeft[16];
                char strRight[16];
                snprintf(strLeft, sizeof(strLeft), "L: %d", leftRemaining);
                snprintf(strRight, sizeof(strRight), "R: %d", rightRemaining);
                updateDisplay(strLeft, strRight);

                if (calibDone)
                    transitionTo(SystemState::RUNNING_NORMAL);
            }
        }
        else if (event.type == EventType::HEARTBEAT)
        {
            HeartbeatMessage hb = event.data.heartbeat;
            if (hb.role == DeviceRole::ANKLE_LEFT)
            {
                leftHeartbeatTime = millis();
                leftBattery = hb.batteryLevel;
            }
            else if (hb.role == DeviceRole::ANKLE_RIGHT)
            {
                rightHeartbeatTime = millis();
                rightBattery = hb.batteryLevel;
            }
        }
    }

    // ── STEP 2: real-time computation and display while running ──
    if (currentState == SystemState::RUNNING_NORMAL || currentState == SystemState::RUNNING_ALERT)
    {
        if (!anklesConnected)
        {
            transitionTo(SystemState::PAUSE);
        }
        else
        {
            float avgLeft = analyzer.getLeftAverage();
            float avgRight = analyzer.getRightAverage();
            float total = avgLeft + avgRight;

            float pctLeft = (total > 0.0f) ? (avgLeft / total) * 100.0f : 50.0f;
            float pctRight = (total > 0.0f) ? (avgRight / total) * 100.0f : 50.0f;

            asymmetry = analyzer.computeAsymmetry(avgLeft, avgRight);

            if (currentState == SystemState::RUNNING_NORMAL && asymmetry > analyzer.getPersonalizedAsymmetryThreshold())
            {
                transitionTo(SystemState::RUNNING_ALERT);
                Hardware::beep(TONE_ALERT_HZ, TONE_ALERT_MS);
            }
            else if (currentState == SystemState::RUNNING_ALERT && asymmetry <= analyzer.getPersonalizedAsymmetryThreshold())
            {
                transitionTo(SystemState::RUNNING_NORMAL);
            }

            if (refreshDue || runningImpactProcessed)
            {
                char strLeft[16];
                char strRight[16];
                snprintf(strLeft, sizeof(strLeft), "L: %.0f%%", pctLeft);
                snprintf(strRight, sizeof(strRight), "R: %.0f%%", pctRight);
                updateDisplay(strLeft, strRight);
                lastDisplayTime = now;
            }
        }
    }
    else if (currentState == SystemState::DIAGNOSTIC || currentState == SystemState::PAUSE)
    {
        if (refreshDue)
        {
            updateDisplay();
            lastDisplayTime = now;
        }
    }

    // ── STEP 3: state machine (physical button) ──
    switch (currentState)
    {
    case SystemState::IDLE:
        if (btnShort)
            transitionTo(SystemState::DIAGNOSTIC);
        if (btnLong)
            transitionTo(SystemState::CALIBRATION);
        break;
    case SystemState::DIAGNOSTIC:
        if (btnShort)
            transitionTo(SystemState::IDLE);
        if (btnLong)
            transitionTo(SystemState::CALIBRATION);
        break;
    case SystemState::CALIBRATION:
        if (btnLong)
            transitionTo(SystemState::IDLE);
        break;
    case SystemState::RUNNING_NORMAL:
    case SystemState::RUNNING_ALERT:
        if (btnShort)
            transitionTo(SystemState::PAUSE);
        if (btnLong)
            transitionTo(SystemState::IDLE);
        break;
    case SystemState::PAUSE:
        if (btnShort)
            transitionTo(SystemState::RUNNING_NORMAL);
        if (btnLong)
            transitionTo(SystemState::IDLE);
        break;
    }

    delay(LOOP_DELAY_MS); // Let the ESP32's FreeRTOS scheduler run other tasks.
}