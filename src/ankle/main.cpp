#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <M5Unified.h>
#include "Protocol.h"
#include "Hardware.h"
#include "GaitAlgorithms.h"

namespace
{
    constexpr uint32_t SAMPLE_INTERVAL_MS = 10;  // 100 Hz accelerometer sampling.
    constexpr uint32_t HEARTBEAT_INTERVAL_MS = 500;
    constexpr uint8_t SPLASH_TEXT_SIZE = 5;

    ImpactDetector detector(DEFAULT_DETECTION_THRESHOLD_G);
    uint32_t lastHeartbeatMs = 0;
    uint32_t seqNum = 0;

    uint32_t lastSampleMs = 0;
}

void setup()
{
    Hardware::init();

    // Splash screen: which side is this unit strapped to.
    M5.Lcd.fillScreen(0x000000);
    M5.Lcd.setTextColor(0xFFFFFF);
    M5.Lcd.setTextSize(SPLASH_TEXT_SIZE);

    int w = M5.Lcd.width();
    int h = M5.Lcd.height();

#if ANKLE_SIDE == 0
    const char *sideText = "LEFT";
#else
    const char *sideText = "RIGHT";
#endif

    int textW = M5.Lcd.textWidth(sideText);
    int textH = M5.Lcd.fontHeight();
    M5.Lcd.setCursor((w - textW) / 2, (h - textH) / 2);
    M5.Lcd.printf("%s", sideText);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
        esp_restart();

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, WRIST_MAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    lastSampleMs = millis();
}

void loop()
{
    Hardware::update();
    uint32_t now = millis();

    // ── STEP 1: strict 100 Hz sampling, drift-free ──
    // Advancing lastSampleMs by a fixed increment (rather than resetting it
    // to `now`) keeps the sample rate accurate even if a loop iteration
    // takes longer than SAMPLE_INTERVAL_MS.
    if (now - lastSampleMs >= SAMPLE_INTERVAL_MS)
    {
        lastSampleMs += SAMPLE_INTERVAL_MS;

        float accel = Hardware::getAccelMagnitude();
        auto peak = detector.processSample(accel, now);

        if (peak.has_value())
        {
            ImpactMessage msg;
            msg.peakForce = peak.value();
            msg.seqNum = seqNum++;
#if ANKLE_SIDE == 0
            msg.isLeft = 1;
#else
            msg.isLeft = 0;
#endif
            esp_now_send(WRIST_MAC, reinterpret_cast<uint8_t *>(&msg), sizeof(msg));
        }
    }

    // ── STEP 2: heartbeat, sent unconditionally every 500 ms ──
    if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS)
    {
        HeartbeatMessage hb;
#if ANKLE_SIDE == 0
        hb.role = DeviceRole::ANKLE_LEFT;
#else
        hb.role = DeviceRole::ANKLE_RIGHT;
#endif
        hb.batteryLevel = M5.Power.getBatteryLevel();

        esp_now_send(WRIST_MAC, reinterpret_cast<uint8_t *>(&hb), sizeof(hb));
        lastHeartbeatMs = now;
    }

    // Small delay to let the ESP32's FreeRTOS scheduler service other tasks.
    delay(1);
}
