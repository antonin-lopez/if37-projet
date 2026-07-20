#pragma once
#include <cstdint>

// ─── SHARED WIRE PROTOCOL (Wrist <-> Ankle units, over ESP-NOW) ───
//
// This header is the single source of truth for anything that crosses
// the radio link: message layouts, device roles and the state machine
// enum. It must be included, unchanged, by every target (wrist / ankle_left
// / ankle_right) so that both sides agree on struct layout.

// ─── IMPACT THRESHOLDS ───
// The ankle unit detects a step locally (DETECTION) using a lower, more
// sensitive threshold; the wrist unit only trusts a step for gait metrics
// once it exceeds VALIDATION. Keeping VALIDATION >= DETECTION prevents the
// wrist from silently discarding every step reported by the ankle.
static constexpr float DEFAULT_DETECTION_THRESHOLD_G = 3.5f;
static constexpr float DEFAULT_VALIDATION_THRESHOLD_G = 4.0f;

enum class SystemState : uint8_t
{
    IDLE,
    DIAGNOSTIC,
    CALIBRATION,
    RUNNING_NORMAL,
    RUNNING_ALERT,
    PAUSE
};

enum class DeviceRole : uint8_t
{
    ANKLE_LEFT,
    ANKLE_RIGHT
};

#pragma pack(push, 1)

// Sent by an ankle unit each time it detects a valid footstrike.
struct ImpactMessage
{
    float peakForce; // Peak acceleration magnitude of the impact, in g.
    uint8_t isLeft;  // 1 if this impact comes from the left ankle, 0 otherwise.
    uint32_t seqNum; // Monotonically increasing per-device sequence number.
};

// Sent periodically by an ankle unit to report liveness and battery level.
struct HeartbeatMessage
{
    DeviceRole role;
    uint8_t batteryLevel; // Percentage, 0-100.
};

#pragma pack(pop)

// MAC address of the wrist unit. Ankle units send all traffic here.
inline const uint8_t WRIST_MAC[6] = {0xC0, 0xCD, 0xD6, 0x14, 0x9E, 0x20};
