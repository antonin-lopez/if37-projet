# Gait Asymmetry Monitor

A real-time running gait-asymmetry monitor built on three M5StickC Plus2
(ESP32) units communicating over ESP-NOW: two ankle-mounted impact sensors
and one wrist unit that fuses their data, displays live feedback, and
alerts the runner when left/right footstrike force becomes abnormally
unbalanced.

Left/right force asymmetry is a common early indicator of compensatory
running form, often linked to fatigue or injury. This project measures it
directly from footstrike impact, live, without any post-run analysis.

## How it works

```
 ┌────────────┐  ESP-NOW   ┌────────────┐
 │ Ankle Left │ ─────────▶ │            │
 └────────────┘            │   Wrist    │  → LCD display, buzzer
 ┌────────────┐  ESP-NOW   │   (hub)    │
 │ Ankle Right│ ─────────▶ │            │
 └────────────┘            └────────────┘
```

- Each **ankle unit** samples its accelerometer at a strict 100 Hz, detects
  footstrike impacts with hysteresis + a refractory period (to reject
  mechanical bounce), and sends the peak impact force to the wrist unit
  over ESP-NOW. It also sends a heartbeat with its battery level every
  500 ms.
- The **wrist unit** receives both streams on its network task, queues
  them into a FreeRTOS queue, and processes everything synchronously in
  the main loop — no shared state is ever touched from two tasks at once.
- During **calibration**, the wrist unit records 16 steps per side to
  establish the runner's own baseline asymmetry, then sets the alert
  threshold to that baseline + a fixed margin. This means alerts trigger
  on *deviation from the runner's normal gait*, not an arbitrary
  one-size-fits-all number.
- While **running**, a rolling window of the last 8 steps per side is used
  to smooth out step-to-step noise, and the live left/right asymmetry is
  compared against the personalized threshold on every step.

## Hardware

- 3x [M5StickC Plus2](https://docs.m5stack.com/en/core/M5StickC%20PLUS2)
  (ESP32-based), one worn on each ankle and one on the wrist.
- No external sensors — impact detection uses the M5StickC's built-in IMU.

## Device states

| State             | Meaning                                              |
|-------------------|-------------------------------------------------------|
| `IDLE`             | Powered on, waiting for user input                    |
| `DIAGNOSTIC`       | Flashes which side triggers on each detected impact    |
| `CALIBRATION`      | Collecting the 16-steps-per-side baseline               |
| `RUNNING_NORMAL`   | Live tracking, asymmetry within personal threshold     |
| `RUNNING_ALERT`    | Live tracking, asymmetry above personal threshold      |
| `PAUSE`            | Running paused, or an ankle unit lost connection        |

Short press / long press on the wrist unit's button drive the transitions
between states (see `src/wrist/main.cpp` for the full state machine).

## Project layout

```
include/Protocol.h         Wire protocol shared by every target (messages, roles, states)
lib/HAL/                   Hardware abstraction layer over M5Unified (display, sensors, input)
lib/Algorithms/            Pure, hardware-independent gait analysis logic
src/wrist/main.cpp         Wrist unit: state machine, display, alerting
src/ankle/main.cpp         Ankle unit: sampling, impact detection, transmission
boards/                    Custom PlatformIO board definition for the M5StickC Plus2
```

`lib/Algorithms` has no dependency on `M5Unified` or Arduino headers, so the
detection and analysis logic can be built and unit-tested independently of
the target hardware.

## Building & flashing

This is a [PlatformIO](https://platformio.org/) project with three build
environments, one per physical unit:

```bash
# Wrist unit
pio run -e wrist -t upload

# Left ankle unit
pio run -e ankle_left -t upload

# Right ankle unit
pio run -e ankle_right -t upload
```

Before flashing, set the wrist unit's MAC address in `include/Protocol.h`
(`WRIST_MAC`) so the ankle units know where to send their data — flash the
wrist unit first, read its MAC address from the serial monitor, then update
the constant and (re-)flash the ankle units.

## Possible next steps

- Persist the calibration baseline to flash so it survives a reboot.
- Log a running session (steps, asymmetry over time) for later review.
- Unit tests for `lib/Algorithms` (no hardware dependency, straightforward
  to test with a native PlatformIO environment).
