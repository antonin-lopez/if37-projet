# Gait Biofeedback System

Real-time running form feedback for runners at risk of overloading one leg after injury. Two ankle-worn sensors and a wrist unit detect footstrike asymmetry and alert the runner during effort, before pain reappears.

Built at the University of Technology of Troyes as part of the IF37 module (responsible design of interactive systems).

## Problem

Most runners have been injured at least once and unconsciously shift load to the healthy leg afterwards. Lab-grade gait analysis (motion capture, pressure mats) is accurate but expensive, occasional, and gives no feedback during the actual run. Consumer running watches log metrics after the fact but do not measure footstrike force precisely enough to catch asymmetry in real time.

## Approach

A distributed system of three M5StickC PLUS2 units (ESP32) communicating over ESP-NOW:

- Two ankle units sample a 3-axis accelerometer at 100 Hz and detect footstrikes locally.
- One wrist unit receives impact data from both ankles, runs a personalized calibration, computes a rolling left/right asymmetry score, and alerts the runner with a screen color change and a beep when asymmetry exceeds their own baseline.

The threshold is personal rather than generic: each runner calibrates their own baseline asymmetry before running, so alerts reflect a deviation from *their* normal gait, not an arbitrary fixed value.

## How it works

1. **Detection** - each ankle unit runs a hysteresis-based impact detector (start above 3.5g, track peak, end below 3.5g, 250 ms refractory period to reject bounce) and sends the peak force over ESP-NOW.
2. **Calibration** - the wrist unit collects 16 valid steps per leg (4.0g validation threshold), computes the baseline asymmetry between both legs, and sets a personal alert threshold as baseline + 10 percentage points.
3. **Running** - each new step feeds an 8-sample rolling average per leg. Asymmetry is recomputed on every update and compared against the personal threshold to trigger or clear an alert.
4. **Feedback** - background color, buzzer tone, and live left/right load percentage on screen.

Full state machine (six states: idle, diagnostic, calibration, running normal, running alert, pause) and message formats are documented in the project synthesis.

## Tech stack

- C++, PlatformIO
- ESP32 (M5StickC PLUS2, M5Unified library)
- ESP-NOW for inter-device wireless communication
- FreeRTOS queue to decouple the network receive callback from the main loop (no shared state between tasks)

## Repository structure

```
include/Protocol.h        shared wire protocol (message structs, roles, state enum)
lib/HAL/                  hardware abstraction over M5Unified (display, sensors, input)
lib/Algorithms/           impact detection and gait asymmetry analysis, hardware-independent
src/wrist/                wrist unit firmware (state machine, display, alerting)
src/ankle/                ankle unit firmware (sampling, impact detection, transmission)
boards/                   custom board definition
platformio.ini            three build targets: wrist, ankle_left, ankle_right
```

The algorithm layer (`lib/Algorithms`) has no hardware dependency and can be unit-tested in isolation.

## Building

```
pio run -e wrist
pio run -e ankle_left
pio run -e ankle_right
```

Each target shares the same codebase and is separated at compile time through build flags (`TARGET_WRIST`, `ANKLE_SIDE`).

## Testing and results

Field-tested outdoors with three runners over mixed terrain. Asymmetry alerts fired correctly in most simulated cases, with under 2 seconds of latency. Two issues came out of testing: false positives on uneven terrain (the fixed threshold and refractory period do not fully reject ground vibration), and a fixed +10 point margin that proved too sensitive for an experienced runner with naturally low asymmetry.

## Known limitations

- Static MAC pairing between ankle units and the wrist, fixed at compile time
- Fixed alert margin, not adjustable per user
- No packet loss detection on ESP-NOW
- Battery life around 80 minutes, short for long runs
- No gyroscope filtering, which could help distinguish real impacts from terrain noise

## Authors

Antonin LOPEZ, Afi ADRAKE, Théo DELOINCE, Florian BORTOLOTTI