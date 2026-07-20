#pragma once
#include <optional>
#include <cstdint>

// Detects a single impact (footstrike) in a stream of acceleration-magnitude
// samples using simple hysteresis: an impact starts when the signal crosses
// above `thresholdG_` and ends when it falls back below it. The returned
// value is the peak magnitude reached during that impact.
class ImpactDetector
{
public:
    // Minimum time, in ms, that must elapse after an impact ends before a
    // new one can start. Filters out mechanical bounce / signal ringing.
    static constexpr uint32_t REFRACTORY_PERIOD_MS = 250;

private:
    float thresholdG_;
    bool isInsideImpact_ = false;
    float currentPeak_ = 0.0f;
    uint32_t lastImpactEndMs_ = 0;

public:
    explicit ImpactDetector(float thresholdG) : thresholdG_(thresholdG) {}

    // Feeds one new sample into the detector. Returns the peak magnitude of
    // a just-completed impact, or std::nullopt if no impact just ended.
    std::optional<float> processSample(float currentSample, uint32_t nowMs);

    void setThreshold(float newThreshold) { thresholdG_ = newThreshold; }
    float getThreshold() const { return thresholdG_; }
};

// Tracks left/right footstrike force over time to (1) establish a
// per-runner baseline during calibration, and (2) continuously compute
// left/right asymmetry once running.
class GaitAnalyzer
{
public:
    static constexpr uint8_t CALIBRATION_STEPS_PER_SIDE = 16;

    // Rolling window size used to smooth out step-to-step noise while running.
    static constexpr uint8_t RUNNING_WINDOW_SIZE = 8;

    // How far above the runner's personal baseline asymmetry (in percentage
    // points) is tolerated before an alert is raised. Chosen empirically to
    // avoid false alerts on normal step-to-step variation.
    static constexpr float ASYMMETRY_ALERT_MARGIN_PERCENT = 10.0f;

private:
    uint8_t leftStepCount_ = 0, rightStepCount_ = 0;
    float leftAccumulator_ = 0.0f, rightAccumulator_ = 0.0f;

    float leftBuffer_[RUNNING_WINDOW_SIZE] = {0.0f};
    float rightBuffer_[RUNNING_WINDOW_SIZE] = {0.0f};
    uint8_t leftBufferIdx_ = 0;
    uint8_t rightBufferIdx_ = 0;
    uint8_t leftBufferCount_ = 0;
    uint8_t rightBufferCount_ = 0;

    float minForceThreshold_;
    float personalizedAsymmetryThreshold_ = ASYMMETRY_ALERT_MARGIN_PERCENT;

public:
    explicit GaitAnalyzer(float minForceThreshold) : minForceThreshold_(minForceThreshold) {}

    // Clears all accumulated state, ready for a fresh calibration pass.
    void reset();

    // Feeds one calibration footstrike. Returns true once both sides have
    // reached CALIBRATION_STEPS_PER_SIDE and personalizedAsymmetryThreshold_
    // has been computed from the runner's baseline.
    bool addCalibrationStep(float force, bool isLeft);

    // Pushes one footstrike force into the rolling window for `isLeft` side.
    void addRunningStep(float force, bool isLeft);

    float getLeftAverage() const;
    float getRightAverage() const;

    // Percentage asymmetry between two force values, e.g. left/right rolling
    // averages, or left/right calibration averages. Returns 0 if either
    // value is below minForceThreshold_ (avoids noise near zero).
    float computeAsymmetry(float left, float right) const;

    uint8_t getTotalSteps() const { return leftStepCount_ + rightStepCount_; }
    uint8_t getLeftStepCount() const { return leftStepCount_; }
    uint8_t getRightStepCount() const { return rightStepCount_; }

    void setMinForceThreshold(float threshold) { minForceThreshold_ = threshold; }
    float getMinForceThreshold() const { return minForceThreshold_; }
    float getPersonalizedAsymmetryThreshold() const { return personalizedAsymmetryThreshold_; }
};
